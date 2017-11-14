/*
 * Copyright © 2012 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

#include <linux/cpufreq.h>
#include <drm/drm_plane_helper.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "../../../platform/x86/intel_ips.h"
#include <linux/module.h>
#include <drm/drm_atomic_helper.h>

/**
 * DOC: RC6
 *
 * RC6 is a special power stage which allows the GPU to enter an very
 * low-voltage mode when idle, using down to 0V while at this stage.  This
 * stage is entered automatically when the GPU is idle when RC6 support is
 * enabled, and as soon as new workload arises GPU wakes up automatically as well.
 *
 * There are different RC6 modes available in Intel GPU, which differentiate
 * among each other with the latency required to enter and leave RC6 and
 * voltage consumed by the GPU in different states.
 *
 * The combination of the following flags define which states GPU is allowed
 * to enter, while RC6 is the normal RC6 state, RC6p is the deep RC6, and
 * RC6pp is deepest RC6. Their support by hardware varies according to the
 * GPU, BIOS, chipset and platform. RC6 is usually the safest one and the one
 * which brings the most power savings; deeper states save more power, but
 * require higher latency to switch to and wake up.
 */
#define INTEL_RC6_ENABLE			(1<<0)
#define INTEL_RC6p_ENABLE			(1<<1)
#define INTEL_RC6pp_ENABLE			(1<<2)

static void gen9_init_clock_gating(struct drm_i915_private *dev_priv)
{
	/* See Bspec note for PSR2_CTL bit 31, Wa#828:skl,bxt,kbl,cfl */
	I915_WRITE(CHICKEN_PAR1_1,
		   I915_READ(CHICKEN_PAR1_1) | SKL_EDP_PSR_FIX_RDWRAP);

	/*
	 * Display WA#0390: skl,bxt,kbl,glk
	 *
	 * Must match Sampler, Pixel Back End, and Media
	 * (0xE194 bit 8, 0x7014 bit 13, 0x4DDC bits 27 and 31).
	 *
	 * Including bits outside the page in the hash would
	 * require 2 (or 4?) MiB alignment of resources. Just
	 * assume the defaul hashing mode which only uses bits
	 * within the page.
	 */
	I915_WRITE(CHICKEN_PAR1_1,
		   I915_READ(CHICKEN_PAR1_1) & ~SKL_RC_HASH_OUTSIDE);

	I915_WRITE(GEN8_CONFIG0,
		   I915_READ(GEN8_CONFIG0) | GEN9_DEFAULT_FIXES);

	/* WaEnableChickenDCPR:skl,bxt,kbl,glk,cfl */
	I915_WRITE(GEN8_CHICKEN_DCPR_1,
		   I915_READ(GEN8_CHICKEN_DCPR_1) | MASK_WAKEMEM);

	/* WaFbcTurnOffFbcWatermark:skl,bxt,kbl,cfl */
	/* WaFbcWakeMemOn:skl,bxt,kbl,glk,cfl */
	I915_WRITE(DISP_ARB_CTL, I915_READ(DISP_ARB_CTL) |
		   DISP_FBC_WM_DIS |
		   DISP_FBC_MEMORY_WAKE);

	/* WaFbcHighMemBwCorruptionAvoidance:skl,bxt,kbl,cfl */
	I915_WRITE(ILK_DPFC_CHICKEN, I915_READ(ILK_DPFC_CHICKEN) |
		   ILK_DPFC_DISABLE_DUMMY0);

	if (IS_SKYLAKE(dev_priv)) {
		/* WaDisableDopClockGating */
		I915_WRITE(GEN7_MISCCPCTL, I915_READ(GEN7_MISCCPCTL)
			   & ~GEN7_DOP_CLOCK_GATE_ENABLE);
	}
}

static void bxt_init_clock_gating(struct drm_i915_private *dev_priv)
{
	gen9_init_clock_gating(dev_priv);

	/* WaDisableSDEUnitClockGating:bxt */
	I915_WRITE(GEN8_UCGCTL6, I915_READ(GEN8_UCGCTL6) |
		   GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/*
	 * FIXME:
	 * GEN8_HDCUNIT_CLOCK_GATE_DISABLE_HDCREQ applies on 3x6 GT SKUs only.
	 */
	I915_WRITE(GEN8_UCGCTL6, I915_READ(GEN8_UCGCTL6) |
		   GEN8_HDCUNIT_CLOCK_GATE_DISABLE_HDCREQ);

	/*
	 * Wa: Backlight PWM may stop in the asserted state, causing backlight
	 * to stay fully on.
	 */
	I915_WRITE(GEN9_CLKGATE_DIS_0, I915_READ(GEN9_CLKGATE_DIS_0) |
		   PWM1_GATING_DIS | PWM2_GATING_DIS);
}

static void glk_init_clock_gating(struct drm_i915_private *dev_priv)
{
	gen9_init_clock_gating(dev_priv);

	/*
	 * WaDisablePWMClockGating:glk
	 * Backlight PWM may stop in the asserted state, causing backlight
	 * to stay fully on.
	 */
	I915_WRITE(GEN9_CLKGATE_DIS_0, I915_READ(GEN9_CLKGATE_DIS_0) |
		   PWM1_GATING_DIS | PWM2_GATING_DIS);

	/* WaDDIIOTimeout:glk */
	if (IS_GLK_REVID(dev_priv, 0, GLK_REVID_A1)) {
		u32 val = I915_READ(CHICKEN_MISC_2);
		val &= ~(GLK_CL0_PWR_DOWN |
			 GLK_CL1_PWR_DOWN |
			 GLK_CL2_PWR_DOWN);
		I915_WRITE(CHICKEN_MISC_2, val);
	}

}

static void i915_pineview_get_mem_freq(struct drm_i915_private *dev_priv)
{
	u32 tmp;

	tmp = I915_READ(CLKCFG);

	switch (tmp & CLKCFG_FSB_MASK) {
	case CLKCFG_FSB_533:
		dev_priv->fsb_freq = 533; /* 133*4 */
		break;
	case CLKCFG_FSB_800:
		dev_priv->fsb_freq = 800; /* 200*4 */
		break;
	case CLKCFG_FSB_667:
		dev_priv->fsb_freq =  667; /* 167*4 */
		break;
	case CLKCFG_FSB_400:
		dev_priv->fsb_freq = 400; /* 100*4 */
		break;
	}

	switch (tmp & CLKCFG_MEM_MASK) {
	case CLKCFG_MEM_533:
		dev_priv->mem_freq = 533;
		break;
	case CLKCFG_MEM_667:
		dev_priv->mem_freq = 667;
		break;
	case CLKCFG_MEM_800:
		dev_priv->mem_freq = 800;
		break;
	}

	/* detect pineview DDR3 setting */
	tmp = I915_READ(CSHRDDR3CTL);
	dev_priv->is_ddr3 = (tmp & CSHRDDR3CTL_DDR3) ? 1 : 0;
}

static void i915_ironlake_get_mem_freq(struct drm_i915_private *dev_priv)
{
	u16 ddrpll, csipll;

	ddrpll = I915_READ16(DDRMPLL1);
	csipll = I915_READ16(CSIPLL0);

	switch (ddrpll & 0xff) {
	case 0xc:
		dev_priv->mem_freq = 800;
		break;
	case 0x10:
		dev_priv->mem_freq = 1066;
		break;
	case 0x14:
		dev_priv->mem_freq = 1333;
		break;
	case 0x18:
		dev_priv->mem_freq = 1600;
		break;
	default:
		DRM_DEBUG_DRIVER("unknown memory frequency 0x%02x\n",
				 ddrpll & 0xff);
		dev_priv->mem_freq = 0;
		break;
	}

	dev_priv->ips.r_t = dev_priv->mem_freq;

	switch (csipll & 0x3ff) {
	case 0x00c:
		dev_priv->fsb_freq = 3200;
		break;
	case 0x00e:
		dev_priv->fsb_freq = 3733;
		break;
	case 0x010:
		dev_priv->fsb_freq = 4266;
		break;
	case 0x012:
		dev_priv->fsb_freq = 4800;
		break;
	case 0x014:
		dev_priv->fsb_freq = 5333;
		break;
	case 0x016:
		dev_priv->fsb_freq = 5866;
		break;
	case 0x018:
		dev_priv->fsb_freq = 6400;
		break;
	default:
		DRM_DEBUG_DRIVER("unknown fsb frequency 0x%04x\n",
				 csipll & 0x3ff);
		dev_priv->fsb_freq = 0;
		break;
	}

	if (dev_priv->fsb_freq == 3200) {
		dev_priv->ips.c_m = 0;
	} else if (dev_priv->fsb_freq > 3200 && dev_priv->fsb_freq <= 4800) {
		dev_priv->ips.c_m = 1;
	} else {
		dev_priv->ips.c_m = 2;
	}
}

static const struct cxsr_latency cxsr_latency_table[] = {
	{1, 0, 800, 400, 3382, 33382, 3983, 33983},    /* DDR2-400 SC */
	{1, 0, 800, 667, 3354, 33354, 3807, 33807},    /* DDR2-667 SC */
	{1, 0, 800, 800, 3347, 33347, 3763, 33763},    /* DDR2-800 SC */
	{1, 1, 800, 667, 6420, 36420, 6873, 36873},    /* DDR3-667 SC */
	{1, 1, 800, 800, 5902, 35902, 6318, 36318},    /* DDR3-800 SC */

	{1, 0, 667, 400, 3400, 33400, 4021, 34021},    /* DDR2-400 SC */
	{1, 0, 667, 667, 3372, 33372, 3845, 33845},    /* DDR2-667 SC */
	{1, 0, 667, 800, 3386, 33386, 3822, 33822},    /* DDR2-800 SC */
	{1, 1, 667, 667, 6438, 36438, 6911, 36911},    /* DDR3-667 SC */
	{1, 1, 667, 800, 5941, 35941, 6377, 36377},    /* DDR3-800 SC */

	{1, 0, 400, 400, 3472, 33472, 4173, 34173},    /* DDR2-400 SC */
	{1, 0, 400, 667, 3443, 33443, 3996, 33996},    /* DDR2-667 SC */
	{1, 0, 400, 800, 3430, 33430, 3946, 33946},    /* DDR2-800 SC */
	{1, 1, 400, 667, 6509, 36509, 7062, 37062},    /* DDR3-667 SC */
	{1, 1, 400, 800, 5985, 35985, 6501, 36501},    /* DDR3-800 SC */

	{0, 0, 800, 400, 3438, 33438, 4065, 34065},    /* DDR2-400 SC */
	{0, 0, 800, 667, 3410, 33410, 3889, 33889},    /* DDR2-667 SC */
	{0, 0, 800, 800, 3403, 33403, 3845, 33845},    /* DDR2-800 SC */
	{0, 1, 800, 667, 6476, 36476, 6955, 36955},    /* DDR3-667 SC */
	{0, 1, 800, 800, 5958, 35958, 6400, 36400},    /* DDR3-800 SC */

	{0, 0, 667, 400, 3456, 33456, 4103, 34106},    /* DDR2-400 SC */
	{0, 0, 667, 667, 3428, 33428, 3927, 33927},    /* DDR2-667 SC */
	{0, 0, 667, 800, 3443, 33443, 3905, 33905},    /* DDR2-800 SC */
	{0, 1, 667, 667, 6494, 36494, 6993, 36993},    /* DDR3-667 SC */
	{0, 1, 667, 800, 5998, 35998, 6460, 36460},    /* DDR3-800 SC */

	{0, 0, 400, 400, 3528, 33528, 4255, 34255},    /* DDR2-400 SC */
	{0, 0, 400, 667, 3500, 33500, 4079, 34079},    /* DDR2-667 SC */
	{0, 0, 400, 800, 3487, 33487, 4029, 34029},    /* DDR2-800 SC */
	{0, 1, 400, 667, 6566, 36566, 7145, 37145},    /* DDR3-667 SC */
	{0, 1, 400, 800, 6042, 36042, 6584, 36584},    /* DDR3-800 SC */
};

static const struct cxsr_latency *intel_get_cxsr_latency(bool is_desktop,
							 bool is_ddr3,
							 int fsb,
							 int mem)
{
	const struct cxsr_latency *latency;
	int i;

	if (fsb == 0 || mem == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(cxsr_latency_table); i++) {
		latency = &cxsr_latency_table[i];
		if (is_desktop == latency->is_desktop &&
		    is_ddr3 == latency->is_ddr3 &&
		    fsb == latency->fsb_freq && mem == latency->mem_freq)
			return latency;
	}

	DRM_DEBUG_KMS("Unknown FSB/MEM found, disable CxSR\n");

	return NULL;
}

static void chv_set_memory_dvfs(struct drm_i915_private *dev_priv, bool enable)
{
	u32 val;

	mutex_lock(&dev_priv->rps.hw_lock);

	val = vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2);
	if (enable)
		val &= ~FORCE_DDR_HIGH_FREQ;
	else
		val |= FORCE_DDR_HIGH_FREQ;
	val &= ~FORCE_DDR_LOW_FREQ;
	val |= FORCE_DDR_FREQ_REQ_ACK;
	vlv_punit_write(dev_priv, PUNIT_REG_DDR_SETUP2, val);

	if (wait_for((vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2) &
		      FORCE_DDR_FREQ_REQ_ACK) == 0, 3))
		DRM_ERROR("timed out waiting for Punit DDR DVFS request\n");

	mutex_unlock(&dev_priv->rps.hw_lock);
}

static void chv_set_memory_pm5(struct drm_i915_private *dev_priv, bool enable)
{
	u32 val;

	mutex_lock(&dev_priv->rps.hw_lock);

	val = vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ);
	if (enable)
		val |= DSP_MAXFIFO_PM5_ENABLE;
	else
		val &= ~DSP_MAXFIFO_PM5_ENABLE;
	vlv_punit_write(dev_priv, PUNIT_REG_DSPFREQ, val);

	mutex_unlock(&dev_priv->rps.hw_lock);
}

#define FW_WM(value, plane) \
	(((value) << DSPFW_ ## plane ## _SHIFT) & DSPFW_ ## plane ## _MASK)

static bool _intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable)
{
	bool was_enabled;
	u32 val;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		was_enabled = I915_READ(FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;
		I915_WRITE(FW_BLC_SELF_VLV, enable ? FW_CSPWRDWNEN : 0);
		POSTING_READ(FW_BLC_SELF_VLV);
	} else if (IS_G4X(dev_priv) || IS_I965GM(dev_priv)) {
		was_enabled = I915_READ(FW_BLC_SELF) & FW_BLC_SELF_EN;
		I915_WRITE(FW_BLC_SELF, enable ? FW_BLC_SELF_EN : 0);
		POSTING_READ(FW_BLC_SELF);
	} else if (IS_PINEVIEW(dev_priv)) {
		val = I915_READ(DSPFW3);
		was_enabled = val & PINEVIEW_SELF_REFRESH_EN;
		if (enable)
			val |= PINEVIEW_SELF_REFRESH_EN;
		else
			val &= ~PINEVIEW_SELF_REFRESH_EN;
		I915_WRITE(DSPFW3, val);
		POSTING_READ(DSPFW3);
	} else if (IS_I945G(dev_priv) || IS_I945GM(dev_priv)) {
		was_enabled = I915_READ(FW_BLC_SELF) & FW_BLC_SELF_EN;
		val = enable ? _MASKED_BIT_ENABLE(FW_BLC_SELF_EN) :
			       _MASKED_BIT_DISABLE(FW_BLC_SELF_EN);
		I915_WRITE(FW_BLC_SELF, val);
		POSTING_READ(FW_BLC_SELF);
	} else if (IS_I915GM(dev_priv)) {
		/*
		 * FIXME can't find a bit like this for 915G, and
		 * and yet it does have the related watermark in
		 * FW_BLC_SELF. What's going on?
		 */
		was_enabled = I915_READ(INSTPM) & INSTPM_SELF_EN;
		val = enable ? _MASKED_BIT_ENABLE(INSTPM_SELF_EN) :
			       _MASKED_BIT_DISABLE(INSTPM_SELF_EN);
		I915_WRITE(INSTPM, val);
		POSTING_READ(INSTPM);
	} else {
		return false;
	}

	trace_intel_memory_cxsr(dev_priv, was_enabled, enable);

	DRM_DEBUG_KMS("memory self-refresh is %s (was %s)\n",
		      enableddisabled(enable),
		      enableddisabled(was_enabled));

	return was_enabled;
}

/**
 * intel_set_memory_cxsr - Configure CxSR state
 * @dev_priv: i915 device
 * @enable: Allow vs. disallow CxSR
 *
 * Allow or disallow the system to enter a special CxSR
 * (C-state self refresh) state. What typically happens in CxSR mode
 * is that several display FIFOs may get combined into a single larger
 * FIFO for a particular plane (so called max FIFO mode) to allow the
 * system to defer memory fetches longer, and the memory will enter
 * self refresh.
 *
 * Note that enabling CxSR does not guarantee that the system enter
 * this special mode, nor does it guarantee that the system stays
 * in that mode once entered. So this just allows/disallows the system
 * to autonomously utilize the CxSR mode. Other factors such as core
 * C-states will affect when/if the system actually enters/exits the
 * CxSR mode.
 *
 * Note that on VLV/CHV this actually only controls the max FIFO mode,
 * and the system is free to enter/exit memory self refresh at any time
 * even when the use of CxSR has been disallowed.
 *
 * While the system is actually in the CxSR/max FIFO mode, some plane
 * control registers will not get latched on vblank. Thus in order to
 * guarantee the system will respond to changes in the plane registers
 * we must always disallow CxSR prior to making changes to those registers.
 * Unfortunately the system will re-evaluate the CxSR conditions at
 * frame start which happens after vblank start (which is when the plane
 * registers would get latched), so we can't proceed with the plane update
 * during the same frame where we disallowed CxSR.
 *
 * Certain platforms also have a deeper HPLL SR mode. Fortunately the
 * HPLL SR mode depends on CxSR itself, so we don't have to hand hold
 * the hardware w.r.t. HPLL SR when writing to plane registers.
 * Disallowing just CxSR is sufficient.
 */
bool intel_set_memory_cxsr(struct drm_i915_private *dev_priv, bool enable)
{
	bool ret;

	mutex_lock(&dev_priv->wm.wm_mutex);
	ret = _intel_set_memory_cxsr(dev_priv, enable);
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		dev_priv->wm.vlv.cxsr = enable;
	else if (IS_G4X(dev_priv))
		dev_priv->wm.g4x.cxsr = enable;
	mutex_unlock(&dev_priv->wm.wm_mutex);

	return ret;
}

/*
 * Latency for FIFO fetches is dependent on several factors:
 *   - memory configuration (speed, channels)
 *   - chipset
 *   - current MCH state
 * It can be fairly high in some situations, so here we assume a fairly
 * pessimal value.  It's a tradeoff between extra memory fetches (if we
 * set this value too high, the FIFO will fetch frequently to stay full)
 * and power consumption (set it too low to save power and we might see
 * FIFO underruns and display "flicker").
 *
 * A value of 5us seems to be a good balance; safe for very low end
 * platforms but not overly aggressive on lower latency configs.
 */
static const int pessimal_latency_ns = 5000;

#define VLV_FIFO_START(dsparb, dsparb2, lo_shift, hi_shift) \
	((((dsparb) >> (lo_shift)) & 0xff) | ((((dsparb2) >> (hi_shift)) & 0x1) << 8))

static void vlv_get_fifo_size(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct vlv_fifo_state *fifo_state = &crtc_state->wm.vlv.fifo_state;
	enum pipe pipe = crtc->pipe;
	int sprite0_start, sprite1_start;

	switch (pipe) {
		uint32_t dsparb, dsparb2, dsparb3;
	case PIPE_A:
		dsparb = I915_READ(DSPARB);
		dsparb2 = I915_READ(DSPARB2);
		sprite0_start = VLV_FIFO_START(dsparb, dsparb2, 0, 0);
		sprite1_start = VLV_FIFO_START(dsparb, dsparb2, 8, 4);
		break;
	case PIPE_B:
		dsparb = I915_READ(DSPARB);
		dsparb2 = I915_READ(DSPARB2);
		sprite0_start = VLV_FIFO_START(dsparb, dsparb2, 16, 8);
		sprite1_start = VLV_FIFO_START(dsparb, dsparb2, 24, 12);
		break;
	case PIPE_C:
		dsparb2 = I915_READ(DSPARB2);
		dsparb3 = I915_READ(DSPARB3);
		sprite0_start = VLV_FIFO_START(dsparb3, dsparb2, 0, 16);
		sprite1_start = VLV_FIFO_START(dsparb3, dsparb2, 8, 20);
		break;
	default:
		MISSING_CASE(pipe);
		return;
	}

	fifo_state->plane[PLANE_PRIMARY] = sprite0_start;
	fifo_state->plane[PLANE_SPRITE0] = sprite1_start - sprite0_start;
	fifo_state->plane[PLANE_SPRITE1] = 511 - sprite1_start;
	fifo_state->plane[PLANE_CURSOR] = 63;
}

static int i9xx_get_fifo_size(struct drm_i915_private *dev_priv, int plane)
{
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	if (plane)
		size = ((dsparb >> DSPARB_CSTART_SHIFT) & 0x7f) - size;

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A", size);

	return size;
}

static int i830_get_fifo_size(struct drm_i915_private *dev_priv, int plane)
{
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x1ff;
	if (plane)
		size = ((dsparb >> DSPARB_BEND_SHIFT) & 0x1ff) - size;
	size >>= 1; /* Convert to cachelines */

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A", size);

	return size;
}

static int i845_get_fifo_size(struct drm_i915_private *dev_priv, int plane)
{
	uint32_t dsparb = I915_READ(DSPARB);
	int size;

	size = dsparb & 0x7f;
	size >>= 2; /* Convert to cachelines */

	DRM_DEBUG_KMS("FIFO size - (0x%08x) %s: %d\n", dsparb,
		      plane ? "B" : "A",
		      size);

	return size;
}

/* Pineview has different values for various configs */
static const struct intel_watermark_params pineview_display_wm = {
	.fifo_size = PINEVIEW_DISPLAY_FIFO,
	.max_wm = PINEVIEW_MAX_WM,
	.default_wm = PINEVIEW_DFT_WM,
	.guard_size = PINEVIEW_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params pineview_display_hplloff_wm = {
	.fifo_size = PINEVIEW_DISPLAY_FIFO,
	.max_wm = PINEVIEW_MAX_WM,
	.default_wm = PINEVIEW_DFT_HPLLOFF_WM,
	.guard_size = PINEVIEW_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params pineview_cursor_wm = {
	.fifo_size = PINEVIEW_CURSOR_FIFO,
	.max_wm = PINEVIEW_CURSOR_MAX_WM,
	.default_wm = PINEVIEW_CURSOR_DFT_WM,
	.guard_size = PINEVIEW_CURSOR_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params pineview_cursor_hplloff_wm = {
	.fifo_size = PINEVIEW_CURSOR_FIFO,
	.max_wm = PINEVIEW_CURSOR_MAX_WM,
	.default_wm = PINEVIEW_CURSOR_DFT_WM,
	.guard_size = PINEVIEW_CURSOR_GUARD_WM,
	.cacheline_size = PINEVIEW_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i965_cursor_wm_info = {
	.fifo_size = I965_CURSOR_FIFO,
	.max_wm = I965_CURSOR_MAX_WM,
	.default_wm = I965_CURSOR_DFT_WM,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i945_wm_info = {
	.fifo_size = I945_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i915_wm_info = {
	.fifo_size = I915_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I915_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i830_a_wm_info = {
	.fifo_size = I855GM_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i830_bc_wm_info = {
	.fifo_size = I855GM_FIFO_SIZE,
	.max_wm = I915_MAX_WM/2,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};
static const struct intel_watermark_params i845_wm_info = {
	.fifo_size = I830_FIFO_SIZE,
	.max_wm = I915_MAX_WM,
	.default_wm = 1,
	.guard_size = 2,
	.cacheline_size = I830_FIFO_LINE_SIZE,
};

/**
 * intel_wm_method1 - Method 1 / "small buffer" watermark formula
 * @pixel_rate: Pipe pixel rate in kHz
 * @cpp: Plane bytes per pixel
 * @latency: Memory wakeup latency in 0.1us units
 *
 * Compute the watermark using the method 1 or "small buffer"
 * formula. The caller may additonally add extra cachelines
 * to account for TLB misses and clock crossings.
 *
 * This method is concerned with the short term drain rate
 * of the FIFO, ie. it does not account for blanking periods
 * which would effectively reduce the average drain rate across
 * a longer period. The name "small" refers to the fact the
 * FIFO is relatively small compared to the amount of data
 * fetched.
 *
 * The FIFO level vs. time graph might look something like:
 *
 *   |\   |\
 *   | \  | \
 * __---__---__ (- plane active, _ blanking)
 * -> time
 *
 * or perhaps like this:
 *
 *   |\|\  |\|\
 * __----__----__ (- plane active, _ blanking)
 * -> time
 *
 * Returns:
 * The watermark in bytes
 */
static unsigned int intel_wm_method1(unsigned int pixel_rate,
				     unsigned int cpp,
				     unsigned int latency)
{
	uint64_t ret;

	ret = (uint64_t) pixel_rate * cpp * latency;
	ret = DIV_ROUND_UP_ULL(ret, 10000);

	return ret;
}

/**
 * intel_wm_method2 - Method 2 / "large buffer" watermark formula
 * @pixel_rate: Pipe pixel rate in kHz
 * @htotal: Pipe horizontal total
 * @width: Plane width in pixels
 * @cpp: Plane bytes per pixel
 * @latency: Memory wakeup latency in 0.1us units
 *
 * Compute the watermark using the method 2 or "large buffer"
 * formula. The caller may additonally add extra cachelines
 * to account for TLB misses and clock crossings.
 *
 * This method is concerned with the long term drain rate
 * of the FIFO, ie. it does account for blanking periods
 * which effectively reduce the average drain rate across
 * a longer period. The name "large" refers to the fact the
 * FIFO is relatively large compared to the amount of data
 * fetched.
 *
 * The FIFO level vs. time graph might look something like:
 *
 *    |\___       |\___
 *    |    \___   |    \___
 *    |        \  |        \
 * __ --__--__--__--__--__--__ (- plane active, _ blanking)
 * -> time
 *
 * Returns:
 * The watermark in bytes
 */
static unsigned int intel_wm_method2(unsigned int pixel_rate,
				     unsigned int htotal,
				     unsigned int width,
				     unsigned int cpp,
				     unsigned int latency)
{
	unsigned int ret;

	/*
	 * FIXME remove once all users are computing
	 * watermarks in the correct place.
	 */
	if (WARN_ON_ONCE(htotal == 0))
		htotal = 1;

	ret = (latency * pixel_rate) / (htotal * 10000);
	ret = (ret + 1) * width * cpp;

	return ret;
}

/**
 * intel_calculate_wm - calculate watermark level
 * @pixel_rate: pixel clock
 * @wm: chip FIFO params
 * @cpp: bytes per pixel
 * @latency_ns: memory latency for the platform
 *
 * Calculate the watermark level (the level at which the display plane will
 * start fetching from memory again).  Each chip has a different display
 * FIFO size and allocation, so the caller needs to figure that out and pass
 * in the correct intel_watermark_params structure.
 *
 * As the pixel clock runs, the FIFO will be drained at a rate that depends
 * on the pixel size.  When it reaches the watermark level, it'll start
 * fetching FIFO line sized based chunks from memory until the FIFO fills
 * past the watermark point.  If the FIFO drains completely, a FIFO underrun
 * will occur, and a display engine hang could result.
 */
static unsigned int intel_calculate_wm(int pixel_rate,
				       const struct intel_watermark_params *wm,
				       int fifo_size, int cpp,
				       unsigned int latency_ns)
{
	int entries, wm_size;

	/*
	 * Note: we need to make sure we don't overflow for various clock &
	 * latency values.
	 * clocks go from a few thousand to several hundred thousand.
	 * latency is usually a few thousand
	 */
	entries = intel_wm_method1(pixel_rate, cpp,
				   latency_ns / 100);
	entries = DIV_ROUND_UP(entries, wm->cacheline_size) +
		wm->guard_size;
	DRM_DEBUG_KMS("FIFO entries required for mode: %d\n", entries);

	wm_size = fifo_size - entries;
	DRM_DEBUG_KMS("FIFO watermark level: %d\n", wm_size);

	/* Don't promote wm_size to unsigned... */
	if (wm_size > wm->max_wm)
		wm_size = wm->max_wm;
	if (wm_size <= 0)
		wm_size = wm->default_wm;

	/*
	 * Bspec seems to indicate that the value shouldn't be lower than
	 * 'burst size + 1'. Certainly 830 is quite unhappy with low values.
	 * Lets go for 8 which is the burst size since certain platforms
	 * already use a hardcoded 8 (which is what the spec says should be
	 * done).
	 */
	if (wm_size <= 8)
		wm_size = 8;

	return wm_size;
}

static bool is_disabling(int old, int new, int threshold)
{
	return old >= threshold && new < threshold;
}

static bool is_enabling(int old, int new, int threshold)
{
	return old < threshold && new >= threshold;
}

static int intel_wm_num_levels(struct drm_i915_private *dev_priv)
{
	return dev_priv->wm.max_level + 1;
}

static bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state,
				   const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);

	/* FIXME check the 'enable' instead */
	if (!crtc_state->base.active)
		return false;

	/*
	 * Treat cursor with fb as always visible since cursor updates
	 * can happen faster than the vrefresh rate, and the current
	 * watermark code doesn't handle that correctly. Cursor updates
	 * which set/clear the fb or change the cursor size are going
	 * to get throttled by intel_legacy_cursor_update() to work
	 * around this problem with the watermark code.
	 */
	if (plane->id == PLANE_CURSOR)
		return plane_state->base.fb != NULL;
	else
		return plane_state->base.visible;
}

static struct intel_crtc *single_enabled_crtc(struct drm_i915_private *dev_priv)
{
	struct intel_crtc *crtc, *enabled = NULL;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		if (intel_crtc_active(crtc)) {
			if (enabled)
				return NULL;
			enabled = crtc;
		}
	}

	return enabled;
}

static void pineview_update_wm(struct intel_crtc *unused_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(unused_crtc->base.dev);
	struct intel_crtc *crtc;
	const struct cxsr_latency *latency;
	u32 reg;
	unsigned int wm;

	latency = intel_get_cxsr_latency(IS_PINEVIEW_G(dev_priv),
					 dev_priv->is_ddr3,
					 dev_priv->fsb_freq,
					 dev_priv->mem_freq);
	if (!latency) {
		DRM_DEBUG_KMS("Unknown FSB/MEM found, disable CxSR\n");
		intel_set_memory_cxsr(dev_priv, false);
		return;
	}

	crtc = single_enabled_crtc(dev_priv);
	if (crtc) {
		const struct drm_display_mode *adjusted_mode =
			&crtc->config->base.adjusted_mode;
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int cpp = fb->format->cpp[0];
		int clock = adjusted_mode->crtc_clock;

		/* Display SR */
		wm = intel_calculate_wm(clock, &pineview_display_wm,
					pineview_display_wm.fifo_size,
					cpp, latency->display_sr);
		reg = I915_READ(DSPFW1);
		reg &= ~DSPFW_SR_MASK;
		reg |= FW_WM(wm, SR);
		I915_WRITE(DSPFW1, reg);
		DRM_DEBUG_KMS("DSPFW1 register is %x\n", reg);

		/* cursor SR */
		wm = intel_calculate_wm(clock, &pineview_cursor_wm,
					pineview_display_wm.fifo_size,
					4, latency->cursor_sr);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_CURSOR_SR_MASK;
		reg |= FW_WM(wm, CURSOR_SR);
		I915_WRITE(DSPFW3, reg);

		/* Display HPLL off SR */
		wm = intel_calculate_wm(clock, &pineview_display_hplloff_wm,
					pineview_display_hplloff_wm.fifo_size,
					cpp, latency->display_hpll_disable);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_HPLL_SR_MASK;
		reg |= FW_WM(wm, HPLL_SR);
		I915_WRITE(DSPFW3, reg);

		/* cursor HPLL off SR */
		wm = intel_calculate_wm(clock, &pineview_cursor_hplloff_wm,
					pineview_display_hplloff_wm.fifo_size,
					4, latency->cursor_hpll_disable);
		reg = I915_READ(DSPFW3);
		reg &= ~DSPFW_HPLL_CURSOR_MASK;
		reg |= FW_WM(wm, HPLL_CURSOR);
		I915_WRITE(DSPFW3, reg);
		DRM_DEBUG_KMS("DSPFW3 register is %x\n", reg);

		intel_set_memory_cxsr(dev_priv, true);
	} else {
		intel_set_memory_cxsr(dev_priv, false);
	}
}

/*
 * Documentation says:
 * "If the line size is small, the TLB fetches can get in the way of the
 *  data fetches, causing some lag in the pixel data return which is not
 *  accounted for in the above formulas. The following adjustment only
 *  needs to be applied if eight whole lines fit in the buffer at once.
 *  The WM is adjusted upwards by the difference between the FIFO size
 *  and the size of 8 whole lines. This adjustment is always performed
 *  in the actual pixel depth regardless of whether FBC is enabled or not."
 */
static int g4x_tlb_miss_wa(int fifo_size, int width, int cpp)
{
	int tlb_miss = fifo_size * 64 - width * cpp * 8;

	return max(0, tlb_miss);
}

static void g4x_write_wm_values(struct drm_i915_private *dev_priv,
				const struct g4x_wm_values *wm)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		trace_g4x_wm(intel_get_crtc_for_pipe(dev_priv, pipe), wm);

	I915_WRITE(DSPFW1,
		   FW_WM(wm->sr.plane, SR) |
		   FW_WM(wm->pipe[PIPE_B].plane[PLANE_CURSOR], CURSORB) |
		   FW_WM(wm->pipe[PIPE_B].plane[PLANE_PRIMARY], PLANEB) |
		   FW_WM(wm->pipe[PIPE_A].plane[PLANE_PRIMARY], PLANEA));
	I915_WRITE(DSPFW2,
		   (wm->fbc_en ? DSPFW_FBC_SR_EN : 0) |
		   FW_WM(wm->sr.fbc, FBC_SR) |
		   FW_WM(wm->hpll.fbc, FBC_HPLL_SR) |
		   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE0], SPRITEB) |
		   FW_WM(wm->pipe[PIPE_A].plane[PLANE_CURSOR], CURSORA) |
		   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE0], SPRITEA));
	I915_WRITE(DSPFW3,
		   (wm->hpll_en ? DSPFW_HPLL_SR_EN : 0) |
		   FW_WM(wm->sr.cursor, CURSOR_SR) |
		   FW_WM(wm->hpll.cursor, HPLL_CURSOR) |
		   FW_WM(wm->hpll.plane, HPLL_SR));

	POSTING_READ(DSPFW1);
}

#define FW_WM_VLV(value, plane) \
	(((value) << DSPFW_ ## plane ## _SHIFT) & DSPFW_ ## plane ## _MASK_VLV)

static void vlv_write_wm_values(struct drm_i915_private *dev_priv,
				const struct vlv_wm_values *wm)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		trace_vlv_wm(intel_get_crtc_for_pipe(dev_priv, pipe), wm);

		I915_WRITE(VLV_DDL(pipe),
			   (wm->ddl[pipe].plane[PLANE_CURSOR] << DDL_CURSOR_SHIFT) |
			   (wm->ddl[pipe].plane[PLANE_SPRITE1] << DDL_SPRITE_SHIFT(1)) |
			   (wm->ddl[pipe].plane[PLANE_SPRITE0] << DDL_SPRITE_SHIFT(0)) |
			   (wm->ddl[pipe].plane[PLANE_PRIMARY] << DDL_PLANE_SHIFT));
	}

	/*
	 * Zero the (unused) WM1 watermarks, and also clear all the
	 * high order bits so that there are no out of bounds values
	 * present in the registers during the reprogramming.
	 */
	I915_WRITE(DSPHOWM, 0);
	I915_WRITE(DSPHOWM1, 0);
	I915_WRITE(DSPFW4, 0);
	I915_WRITE(DSPFW5, 0);
	I915_WRITE(DSPFW6, 0);

	I915_WRITE(DSPFW1,
		   FW_WM(wm->sr.plane, SR) |
		   FW_WM(wm->pipe[PIPE_B].plane[PLANE_CURSOR], CURSORB) |
		   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_PRIMARY], PLANEB) |
		   FW_WM_VLV(wm->pipe[PIPE_A].plane[PLANE_PRIMARY], PLANEA));
	I915_WRITE(DSPFW2,
		   FW_WM_VLV(wm->pipe[PIPE_A].plane[PLANE_SPRITE1], SPRITEB) |
		   FW_WM(wm->pipe[PIPE_A].plane[PLANE_CURSOR], CURSORA) |
		   FW_WM_VLV(wm->pipe[PIPE_A].plane[PLANE_SPRITE0], SPRITEA));
	I915_WRITE(DSPFW3,
		   FW_WM(wm->sr.cursor, CURSOR_SR));

	if (IS_CHERRYVIEW(dev_priv)) {
		I915_WRITE(DSPFW7_CHV,
			   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE1], SPRITED) |
			   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE0], SPRITEC));
		I915_WRITE(DSPFW8_CHV,
			   FW_WM_VLV(wm->pipe[PIPE_C].plane[PLANE_SPRITE1], SPRITEF) |
			   FW_WM_VLV(wm->pipe[PIPE_C].plane[PLANE_SPRITE0], SPRITEE));
		I915_WRITE(DSPFW9_CHV,
			   FW_WM_VLV(wm->pipe[PIPE_C].plane[PLANE_PRIMARY], PLANEC) |
			   FW_WM(wm->pipe[PIPE_C].plane[PLANE_CURSOR], CURSORC));
		I915_WRITE(DSPHOWM,
			   FW_WM(wm->sr.plane >> 9, SR_HI) |
			   FW_WM(wm->pipe[PIPE_C].plane[PLANE_SPRITE1] >> 8, SPRITEF_HI) |
			   FW_WM(wm->pipe[PIPE_C].plane[PLANE_SPRITE0] >> 8, SPRITEE_HI) |
			   FW_WM(wm->pipe[PIPE_C].plane[PLANE_PRIMARY] >> 8, PLANEC_HI) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE1] >> 8, SPRITED_HI) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE0] >> 8, SPRITEC_HI) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_PRIMARY] >> 8, PLANEB_HI) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE1] >> 8, SPRITEB_HI) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE0] >> 8, SPRITEA_HI) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_PRIMARY] >> 8, PLANEA_HI));
	} else {
		I915_WRITE(DSPFW7,
			   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE1], SPRITED) |
			   FW_WM_VLV(wm->pipe[PIPE_B].plane[PLANE_SPRITE0], SPRITEC));
		I915_WRITE(DSPHOWM,
			   FW_WM(wm->sr.plane >> 9, SR_HI) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE1] >> 8, SPRITED_HI) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_SPRITE0] >> 8, SPRITEC_HI) |
			   FW_WM(wm->pipe[PIPE_B].plane[PLANE_PRIMARY] >> 8, PLANEB_HI) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE1] >> 8, SPRITEB_HI) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_SPRITE0] >> 8, SPRITEA_HI) |
			   FW_WM(wm->pipe[PIPE_A].plane[PLANE_PRIMARY] >> 8, PLANEA_HI));
	}

	POSTING_READ(DSPFW1);
}

#undef FW_WM_VLV

static void g4x_setup_wm_latency(struct drm_i915_private *dev_priv)
{
	/* all latencies in usec */
	dev_priv->wm.pri_latency[G4X_WM_LEVEL_NORMAL] = 5;
	dev_priv->wm.pri_latency[G4X_WM_LEVEL_SR] = 12;
	dev_priv->wm.pri_latency[G4X_WM_LEVEL_HPLL] = 35;

	dev_priv->wm.max_level = G4X_WM_LEVEL_HPLL;
}

static int g4x_plane_fifo_size(enum plane_id plane_id, int level)
{
	/*
	 * DSPCNTR[13] supposedly controls whether the
	 * primary plane can use the FIFO space otherwise
	 * reserved for the sprite plane. It's not 100% clear
	 * what the actual FIFO size is, but it looks like we
	 * can happily set both primary and sprite watermarks
	 * up to 127 cachelines. So that would seem to mean
	 * that either DSPCNTR[13] doesn't do anything, or that
	 * the total FIFO is >= 256 cachelines in size. Either
	 * way, we don't seem to have to worry about this
	 * repartitioning as the maximum watermark value the
	 * register can hold for each plane is lower than the
	 * minimum FIFO size.
	 */
	switch (plane_id) {
	case PLANE_CURSOR:
		return 63;
	case PLANE_PRIMARY:
		return level == G4X_WM_LEVEL_NORMAL ? 127 : 511;
	case PLANE_SPRITE0:
		return level == G4X_WM_LEVEL_NORMAL ? 127 : 0;
	default:
		MISSING_CASE(plane_id);
		return 0;
	}
}

static int g4x_fbc_fifo_size(int level)
{
	switch (level) {
	case G4X_WM_LEVEL_SR:
		return 7;
	case G4X_WM_LEVEL_HPLL:
		return 15;
	default:
		MISSING_CASE(level);
		return 0;
	}
}

static uint16_t g4x_compute_wm(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state,
			       int level)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	int clock, htotal, cpp, width, wm;
	int latency = dev_priv->wm.pri_latency[level] * 10;

	if (latency == 0)
		return USHRT_MAX;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	/*
	 * Not 100% sure which way ELK should go here as the
	 * spec only says CL/CTG should assume 32bpp and BW
	 * doesn't need to. But as these things followed the
	 * mobile vs. desktop lines on gen3 as well, let's
	 * assume ELK doesn't need this.
	 *
	 * The spec also fails to list such a restriction for
	 * the HPLL watermark, which seems a little strange.
	 * Let's use 32bpp for the HPLL watermark as well.
	 */
	if (IS_GM45(dev_priv) && plane->id == PLANE_PRIMARY &&
	    level != G4X_WM_LEVEL_NORMAL)
		cpp = 4;
	else
		cpp = plane_state->base.fb->format->cpp[0];

	clock = adjusted_mode->crtc_clock;
	htotal = adjusted_mode->crtc_htotal;

	if (plane->id == PLANE_CURSOR)
		width = plane_state->base.crtc_w;
	else
		width = drm_rect_width(&plane_state->base.dst);

	if (plane->id == PLANE_CURSOR) {
		wm = intel_wm_method2(clock, htotal, width, cpp, latency);
	} else if (plane->id == PLANE_PRIMARY &&
		   level == G4X_WM_LEVEL_NORMAL) {
		wm = intel_wm_method1(clock, cpp, latency);
	} else {
		int small, large;

		small = intel_wm_method1(clock, cpp, latency);
		large = intel_wm_method2(clock, htotal, width, cpp, latency);

		wm = min(small, large);
	}

	wm += g4x_tlb_miss_wa(g4x_plane_fifo_size(plane->id, level),
			      width, cpp);

	wm = DIV_ROUND_UP(wm, 64) + 2;

	return min_t(int, wm, USHRT_MAX);
}

static bool g4x_raw_plane_wm_set(struct intel_crtc_state *crtc_state,
				 int level, enum plane_id plane_id, u16 value)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	bool dirty = false;

	for (; level < intel_wm_num_levels(dev_priv); level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];

		dirty |= raw->plane[plane_id] != value;
		raw->plane[plane_id] = value;
	}

	return dirty;
}

static bool g4x_raw_fbc_wm_set(struct intel_crtc_state *crtc_state,
			       int level, u16 value)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	bool dirty = false;

	/* NORMAL level doesn't have an FBC watermark */
	level = max(level, G4X_WM_LEVEL_SR);

	for (; level < intel_wm_num_levels(dev_priv); level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];

		dirty |= raw->fbc != value;
		raw->fbc = value;
	}

	return dirty;
}

static uint32_t ilk_compute_fbc_wm(const struct intel_crtc_state *cstate,
				   const struct intel_plane_state *pstate,
				   uint32_t pri_val);

static bool g4x_raw_plane_wm_compute(struct intel_crtc_state *crtc_state,
				     const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	int num_levels = intel_wm_num_levels(to_i915(plane->base.dev));
	enum plane_id plane_id = plane->id;
	bool dirty = false;
	int level;

	if (!intel_wm_plane_visible(crtc_state, plane_state)) {
		dirty |= g4x_raw_plane_wm_set(crtc_state, 0, plane_id, 0);
		if (plane_id == PLANE_PRIMARY)
			dirty |= g4x_raw_fbc_wm_set(crtc_state, 0, 0);
		goto out;
	}

	for (level = 0; level < num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];
		int wm, max_wm;

		wm = g4x_compute_wm(crtc_state, plane_state, level);
		max_wm = g4x_plane_fifo_size(plane_id, level);

		if (wm > max_wm)
			break;

		dirty |= raw->plane[plane_id] != wm;
		raw->plane[plane_id] = wm;

		if (plane_id != PLANE_PRIMARY ||
		    level == G4X_WM_LEVEL_NORMAL)
			continue;

		wm = ilk_compute_fbc_wm(crtc_state, plane_state,
					raw->plane[plane_id]);
		max_wm = g4x_fbc_fifo_size(level);

		/*
		 * FBC wm is not mandatory as we
		 * can always just disable its use.
		 */
		if (wm > max_wm)
			wm = USHRT_MAX;

		dirty |= raw->fbc != wm;
		raw->fbc = wm;
	}

	/* mark watermarks as invalid */
	dirty |= g4x_raw_plane_wm_set(crtc_state, level, plane_id, USHRT_MAX);

	if (plane_id == PLANE_PRIMARY)
		dirty |= g4x_raw_fbc_wm_set(crtc_state, level, USHRT_MAX);

 out:
	if (dirty) {
		DRM_DEBUG_KMS("%s watermarks: normal=%d, SR=%d, HPLL=%d\n",
			      plane->base.name,
			      crtc_state->wm.g4x.raw[G4X_WM_LEVEL_NORMAL].plane[plane_id],
			      crtc_state->wm.g4x.raw[G4X_WM_LEVEL_SR].plane[plane_id],
			      crtc_state->wm.g4x.raw[G4X_WM_LEVEL_HPLL].plane[plane_id]);

		if (plane_id == PLANE_PRIMARY)
			DRM_DEBUG_KMS("FBC watermarks: SR=%d, HPLL=%d\n",
				      crtc_state->wm.g4x.raw[G4X_WM_LEVEL_SR].fbc,
				      crtc_state->wm.g4x.raw[G4X_WM_LEVEL_HPLL].fbc);
	}

	return dirty;
}

static bool g4x_raw_plane_wm_is_valid(const struct intel_crtc_state *crtc_state,
				      enum plane_id plane_id, int level)
{
	const struct g4x_pipe_wm *raw = &crtc_state->wm.g4x.raw[level];

	return raw->plane[plane_id] <= g4x_plane_fifo_size(plane_id, level);
}

static bool g4x_raw_crtc_wm_is_valid(const struct intel_crtc_state *crtc_state,
				     int level)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);

	if (level > dev_priv->wm.max_level)
		return false;

	return g4x_raw_plane_wm_is_valid(crtc_state, PLANE_PRIMARY, level) &&
		g4x_raw_plane_wm_is_valid(crtc_state, PLANE_SPRITE0, level) &&
		g4x_raw_plane_wm_is_valid(crtc_state, PLANE_CURSOR, level);
}

/* mark all levels starting from 'level' as invalid */
static void g4x_invalidate_wms(struct intel_crtc *crtc,
			       struct g4x_wm_state *wm_state, int level)
{
	if (level <= G4X_WM_LEVEL_NORMAL) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id)
			wm_state->wm.plane[plane_id] = USHRT_MAX;
	}

	if (level <= G4X_WM_LEVEL_SR) {
		wm_state->cxsr = false;
		wm_state->sr.cursor = USHRT_MAX;
		wm_state->sr.plane = USHRT_MAX;
		wm_state->sr.fbc = USHRT_MAX;
	}

	if (level <= G4X_WM_LEVEL_HPLL) {
		wm_state->hpll_en = false;
		wm_state->hpll.cursor = USHRT_MAX;
		wm_state->hpll.plane = USHRT_MAX;
		wm_state->hpll.fbc = USHRT_MAX;
	}
}

static int g4x_compute_pipe_wm(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct intel_atomic_state *state =
		to_intel_atomic_state(crtc_state->base.state);
	struct g4x_wm_state *wm_state = &crtc_state->wm.g4x.optimal;
	int num_active_planes = hweight32(crtc_state->active_planes &
					  ~BIT(PLANE_CURSOR));
	const struct g4x_pipe_wm *raw;
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	enum plane_id plane_id;
	int i, level;
	unsigned int dirty = 0;

	for_each_intel_plane_in_state(state, plane, plane_state, i) {
		const struct intel_plane_state *old_plane_state =
			to_intel_plane_state(plane->base.state);

		if (plane_state->base.crtc != &crtc->base &&
		    old_plane_state->base.crtc != &crtc->base)
			continue;

		if (g4x_raw_plane_wm_compute(crtc_state, plane_state))
			dirty |= BIT(plane->id);
	}

	if (!dirty)
		return 0;

	level = G4X_WM_LEVEL_NORMAL;
	if (!g4x_raw_crtc_wm_is_valid(crtc_state, level))
		goto out;

	raw = &crtc_state->wm.g4x.raw[level];
	for_each_plane_id_on_crtc(crtc, plane_id)
		wm_state->wm.plane[plane_id] = raw->plane[plane_id];

	level = G4X_WM_LEVEL_SR;

	if (!g4x_raw_crtc_wm_is_valid(crtc_state, level))
		goto out;

	raw = &crtc_state->wm.g4x.raw[level];
	wm_state->sr.plane = raw->plane[PLANE_PRIMARY];
	wm_state->sr.cursor = raw->plane[PLANE_CURSOR];
	wm_state->sr.fbc = raw->fbc;

	wm_state->cxsr = num_active_planes == BIT(PLANE_PRIMARY);

	level = G4X_WM_LEVEL_HPLL;

	if (!g4x_raw_crtc_wm_is_valid(crtc_state, level))
		goto out;

	raw = &crtc_state->wm.g4x.raw[level];
	wm_state->hpll.plane = raw->plane[PLANE_PRIMARY];
	wm_state->hpll.cursor = raw->plane[PLANE_CURSOR];
	wm_state->hpll.fbc = raw->fbc;

	wm_state->hpll_en = wm_state->cxsr;

	level++;

 out:
	if (level == G4X_WM_LEVEL_NORMAL)
		return -EINVAL;

	/* invalidate the higher levels */
	g4x_invalidate_wms(crtc, wm_state, level);

	/*
	 * Determine if the FBC watermark(s) can be used. IF
	 * this isn't the case we prefer to disable the FBC
	 ( watermark(s) rather than disable the SR/HPLL
	 * level(s) entirely.
	 */
	wm_state->fbc_en = level > G4X_WM_LEVEL_NORMAL;

	if (level >= G4X_WM_LEVEL_SR &&
	    wm_state->sr.fbc > g4x_fbc_fifo_size(G4X_WM_LEVEL_SR))
		wm_state->fbc_en = false;
	else if (level >= G4X_WM_LEVEL_HPLL &&
		 wm_state->hpll.fbc > g4x_fbc_fifo_size(G4X_WM_LEVEL_HPLL))
		wm_state->fbc_en = false;

	return 0;
}

static int g4x_compute_intermediate_wm(struct drm_device *dev,
				       struct intel_crtc *crtc,
				       struct intel_crtc_state *crtc_state)
{
	struct g4x_wm_state *intermediate = &crtc_state->wm.g4x.intermediate;
	const struct g4x_wm_state *optimal = &crtc_state->wm.g4x.optimal;
	const struct g4x_wm_state *active = &crtc->wm.active.g4x;
	enum plane_id plane_id;

	intermediate->cxsr = optimal->cxsr && active->cxsr &&
		!crtc_state->disable_cxsr;
	intermediate->hpll_en = optimal->hpll_en && active->hpll_en &&
		!crtc_state->disable_cxsr;
	intermediate->fbc_en = optimal->fbc_en && active->fbc_en;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		intermediate->wm.plane[plane_id] =
			max(optimal->wm.plane[plane_id],
			    active->wm.plane[plane_id]);

		WARN_ON(intermediate->wm.plane[plane_id] >
			g4x_plane_fifo_size(plane_id, G4X_WM_LEVEL_NORMAL));
	}

	intermediate->sr.plane = max(optimal->sr.plane,
				     active->sr.plane);
	intermediate->sr.cursor = max(optimal->sr.cursor,
				      active->sr.cursor);
	intermediate->sr.fbc = max(optimal->sr.fbc,
				   active->sr.fbc);

	intermediate->hpll.plane = max(optimal->hpll.plane,
				       active->hpll.plane);
	intermediate->hpll.cursor = max(optimal->hpll.cursor,
					active->hpll.cursor);
	intermediate->hpll.fbc = max(optimal->hpll.fbc,
				     active->hpll.fbc);

	WARN_ON((intermediate->sr.plane >
		 g4x_plane_fifo_size(PLANE_PRIMARY, G4X_WM_LEVEL_SR) ||
		 intermediate->sr.cursor >
		 g4x_plane_fifo_size(PLANE_CURSOR, G4X_WM_LEVEL_SR)) &&
		intermediate->cxsr);
	WARN_ON((intermediate->sr.plane >
		 g4x_plane_fifo_size(PLANE_PRIMARY, G4X_WM_LEVEL_HPLL) ||
		 intermediate->sr.cursor >
		 g4x_plane_fifo_size(PLANE_CURSOR, G4X_WM_LEVEL_HPLL)) &&
		intermediate->hpll_en);

	WARN_ON(intermediate->sr.fbc > g4x_fbc_fifo_size(1) &&
		intermediate->fbc_en && intermediate->cxsr);
	WARN_ON(intermediate->hpll.fbc > g4x_fbc_fifo_size(2) &&
		intermediate->fbc_en && intermediate->hpll_en);

	/*
	 * If our intermediate WM are identical to the final WM, then we can
	 * omit the post-vblank programming; only update if it's different.
	 */
	if (memcmp(intermediate, optimal, sizeof(*intermediate)) != 0)
		crtc_state->wm.need_postvbl_update = true;

	return 0;
}

static void g4x_merge_wm(struct drm_i915_private *dev_priv,
			 struct g4x_wm_values *wm)
{
	struct intel_crtc *crtc;
	int num_active_crtcs = 0;

	wm->cxsr = true;
	wm->hpll_en = true;
	wm->fbc_en = true;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct g4x_wm_state *wm_state = &crtc->wm.active.g4x;

		if (!crtc->active)
			continue;

		if (!wm_state->cxsr)
			wm->cxsr = false;
		if (!wm_state->hpll_en)
			wm->hpll_en = false;
		if (!wm_state->fbc_en)
			wm->fbc_en = false;

		num_active_crtcs++;
	}

	if (num_active_crtcs != 1) {
		wm->cxsr = false;
		wm->hpll_en = false;
		wm->fbc_en = false;
	}

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct g4x_wm_state *wm_state = &crtc->wm.active.g4x;
		enum pipe pipe = crtc->pipe;

		wm->pipe[pipe] = wm_state->wm;
		if (crtc->active && wm->cxsr)
			wm->sr = wm_state->sr;
		if (crtc->active && wm->hpll_en)
			wm->hpll = wm_state->hpll;
	}
}

static void g4x_program_watermarks(struct drm_i915_private *dev_priv)
{
	struct g4x_wm_values *old_wm = &dev_priv->wm.g4x;
	struct g4x_wm_values new_wm = {};

	g4x_merge_wm(dev_priv, &new_wm);

	if (memcmp(old_wm, &new_wm, sizeof(new_wm)) == 0)
		return;

	if (is_disabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, false);

	g4x_write_wm_values(dev_priv, &new_wm);

	if (is_enabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, true);

	*old_wm = new_wm;
}

static void g4x_initial_watermarks(struct intel_atomic_state *state,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);

	mutex_lock(&dev_priv->wm.wm_mutex);
	crtc->wm.active.g4x = crtc_state->wm.g4x.intermediate;
	g4x_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->wm.wm_mutex);
}

static void g4x_optimize_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);

	if (!crtc_state->wm.need_postvbl_update)
		return;

	mutex_lock(&dev_priv->wm.wm_mutex);
	intel_crtc->wm.active.g4x = crtc_state->wm.g4x.optimal;
	g4x_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->wm.wm_mutex);
}

/* latency must be in 0.1us units. */
static unsigned int vlv_wm_method2(unsigned int pixel_rate,
				   unsigned int htotal,
				   unsigned int width,
				   unsigned int cpp,
				   unsigned int latency)
{
	unsigned int ret;

	ret = intel_wm_method2(pixel_rate, htotal,
			       width, cpp, latency);
	ret = DIV_ROUND_UP(ret, 64);

	return ret;
}

static void vlv_setup_wm_latency(struct drm_i915_private *dev_priv)
{
	/* all latencies in usec */
	dev_priv->wm.pri_latency[VLV_WM_LEVEL_PM2] = 3;

	dev_priv->wm.max_level = VLV_WM_LEVEL_PM2;

	if (IS_CHERRYVIEW(dev_priv)) {
		dev_priv->wm.pri_latency[VLV_WM_LEVEL_PM5] = 12;
		dev_priv->wm.pri_latency[VLV_WM_LEVEL_DDR_DVFS] = 33;

		dev_priv->wm.max_level = VLV_WM_LEVEL_DDR_DVFS;
	}
}

static uint16_t vlv_compute_wm_level(const struct intel_crtc_state *crtc_state,
				     const struct intel_plane_state *plane_state,
				     int level)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->base.adjusted_mode;
	int clock, htotal, cpp, width, wm;

	if (dev_priv->wm.pri_latency[level] == 0)
		return USHRT_MAX;

	if (!intel_wm_plane_visible(crtc_state, plane_state))
		return 0;

	cpp = plane_state->base.fb->format->cpp[0];
	clock = adjusted_mode->crtc_clock;
	htotal = adjusted_mode->crtc_htotal;
	width = crtc_state->pipe_src_w;

	if (plane->id == PLANE_CURSOR) {
		/*
		 * FIXME the formula gives values that are
		 * too big for the cursor FIFO, and hence we
		 * would never be able to use cursors. For
		 * now just hardcode the watermark.
		 */
		wm = 63;
	} else {
		wm = vlv_wm_method2(clock, htotal, width, cpp,
				    dev_priv->wm.pri_latency[level] * 10);
	}

	return min_t(int, wm, USHRT_MAX);
}

static bool vlv_need_sprite0_fifo_workaround(unsigned int active_planes)
{
	return (active_planes & (BIT(PLANE_SPRITE0) |
				 BIT(PLANE_SPRITE1))) == BIT(PLANE_SPRITE1);
}

static int vlv_compute_fifo(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	const struct g4x_pipe_wm *raw =
		&crtc_state->wm.vlv.raw[VLV_WM_LEVEL_PM2];
	struct vlv_fifo_state *fifo_state = &crtc_state->wm.vlv.fifo_state;
	unsigned int active_planes = crtc_state->active_planes & ~BIT(PLANE_CURSOR);
	int num_active_planes = hweight32(active_planes);
	const int fifo_size = 511;
	int fifo_extra, fifo_left = fifo_size;
	int sprite0_fifo_extra = 0;
	unsigned int total_rate;
	enum plane_id plane_id;

	/*
	 * When enabling sprite0 after sprite1 has already been enabled
	 * we tend to get an underrun unless sprite0 already has some
	 * FIFO space allcoated. Hence we always allocate at least one
	 * cacheline for sprite0 whenever sprite1 is enabled.
	 *
	 * All other plane enable sequences appear immune to this problem.
	 */
	if (vlv_need_sprite0_fifo_workaround(active_planes))
		sprite0_fifo_extra = 1;

	total_rate = raw->plane[PLANE_PRIMARY] +
		raw->plane[PLANE_SPRITE0] +
		raw->plane[PLANE_SPRITE1] +
		sprite0_fifo_extra;

	if (total_rate > fifo_size)
		return -EINVAL;

	if (total_rate == 0)
		total_rate = 1;

	for_each_plane_id_on_crtc(crtc, plane_id) {
		unsigned int rate;

		if ((active_planes & BIT(plane_id)) == 0) {
			fifo_state->plane[plane_id] = 0;
			continue;
		}

		rate = raw->plane[plane_id];
		fifo_state->plane[plane_id] = fifo_size * rate / total_rate;
		fifo_left -= fifo_state->plane[plane_id];
	}

	fifo_state->plane[PLANE_SPRITE0] += sprite0_fifo_extra;
	fifo_left -= sprite0_fifo_extra;

	fifo_state->plane[PLANE_CURSOR] = 63;

	fifo_extra = DIV_ROUND_UP(fifo_left, num_active_planes ?: 1);

	/* spread the remainder evenly */
	for_each_plane_id_on_crtc(crtc, plane_id) {
		int plane_extra;

		if (fifo_left == 0)
			break;

		if ((active_planes & BIT(plane_id)) == 0)
			continue;

		plane_extra = min(fifo_extra, fifo_left);
		fifo_state->plane[plane_id] += plane_extra;
		fifo_left -= plane_extra;
	}

	WARN_ON(active_planes != 0 && fifo_left != 0);

	/* give it all to the first plane if none are active */
	if (active_planes == 0) {
		WARN_ON(fifo_left != fifo_size);
		fifo_state->plane[PLANE_PRIMARY] = fifo_left;
	}

	return 0;
}

/* mark all levels starting from 'level' as invalid */
static void vlv_invalidate_wms(struct intel_crtc *crtc,
			       struct vlv_wm_state *wm_state, int level)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	for (; level < intel_wm_num_levels(dev_priv); level++) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id)
			wm_state->wm[level].plane[plane_id] = USHRT_MAX;

		wm_state->sr[level].cursor = USHRT_MAX;
		wm_state->sr[level].plane = USHRT_MAX;
	}
}

static u16 vlv_invert_wm_value(u16 wm, u16 fifo_size)
{
	if (wm > fifo_size)
		return USHRT_MAX;
	else
		return fifo_size - wm;
}

/*
 * Starting from 'level' set all higher
 * levels to 'value' in the "raw" watermarks.
 */
static bool vlv_raw_plane_wm_set(struct intel_crtc_state *crtc_state,
				 int level, enum plane_id plane_id, u16 value)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	int num_levels = intel_wm_num_levels(dev_priv);
	bool dirty = false;

	for (; level < num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.vlv.raw[level];

		dirty |= raw->plane[plane_id] != value;
		raw->plane[plane_id] = value;
	}

	return dirty;
}

static bool vlv_raw_plane_wm_compute(struct intel_crtc_state *crtc_state,
				     const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->base.plane);
	enum plane_id plane_id = plane->id;
	int num_levels = intel_wm_num_levels(to_i915(plane->base.dev));
	int level;
	bool dirty = false;

	if (!intel_wm_plane_visible(crtc_state, plane_state)) {
		dirty |= vlv_raw_plane_wm_set(crtc_state, 0, plane_id, 0);
		goto out;
	}

	for (level = 0; level < num_levels; level++) {
		struct g4x_pipe_wm *raw = &crtc_state->wm.vlv.raw[level];
		int wm = vlv_compute_wm_level(crtc_state, plane_state, level);
		int max_wm = plane_id == PLANE_CURSOR ? 63 : 511;

		if (wm > max_wm)
			break;

		dirty |= raw->plane[plane_id] != wm;
		raw->plane[plane_id] = wm;
	}

	/* mark all higher levels as invalid */
	dirty |= vlv_raw_plane_wm_set(crtc_state, level, plane_id, USHRT_MAX);

out:
	if (dirty)
		DRM_DEBUG_KMS("%s watermarks: PM2=%d, PM5=%d, DDR DVFS=%d\n",
			      plane->base.name,
			      crtc_state->wm.vlv.raw[VLV_WM_LEVEL_PM2].plane[plane_id],
			      crtc_state->wm.vlv.raw[VLV_WM_LEVEL_PM5].plane[plane_id],
			      crtc_state->wm.vlv.raw[VLV_WM_LEVEL_DDR_DVFS].plane[plane_id]);

	return dirty;
}

static bool vlv_raw_plane_wm_is_valid(const struct intel_crtc_state *crtc_state,
				      enum plane_id plane_id, int level)
{
	const struct g4x_pipe_wm *raw =
		&crtc_state->wm.vlv.raw[level];
	const struct vlv_fifo_state *fifo_state =
		&crtc_state->wm.vlv.fifo_state;

	return raw->plane[plane_id] <= fifo_state->plane[plane_id];
}

static bool vlv_raw_crtc_wm_is_valid(const struct intel_crtc_state *crtc_state, int level)
{
	return vlv_raw_plane_wm_is_valid(crtc_state, PLANE_PRIMARY, level) &&
		vlv_raw_plane_wm_is_valid(crtc_state, PLANE_SPRITE0, level) &&
		vlv_raw_plane_wm_is_valid(crtc_state, PLANE_SPRITE1, level) &&
		vlv_raw_plane_wm_is_valid(crtc_state, PLANE_CURSOR, level);
}

static int vlv_compute_pipe_wm(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_atomic_state *state =
		to_intel_atomic_state(crtc_state->base.state);
	struct vlv_wm_state *wm_state = &crtc_state->wm.vlv.optimal;
	const struct vlv_fifo_state *fifo_state =
		&crtc_state->wm.vlv.fifo_state;
	int num_active_planes = hweight32(crtc_state->active_planes &
					  ~BIT(PLANE_CURSOR));
	bool needs_modeset = drm_atomic_crtc_needs_modeset(&crtc_state->base);
	struct intel_plane_state *plane_state;
	struct intel_plane *plane;
	enum plane_id plane_id;
	int level, ret, i;
	unsigned int dirty = 0;

	for_each_intel_plane_in_state(state, plane, plane_state, i) {
		const struct intel_plane_state *old_plane_state =
			to_intel_plane_state(plane->base.state);

		if (plane_state->base.crtc != &crtc->base &&
		    old_plane_state->base.crtc != &crtc->base)
			continue;

		if (vlv_raw_plane_wm_compute(crtc_state, plane_state))
			dirty |= BIT(plane->id);
	}

	/*
	 * DSPARB registers may have been reset due to the
	 * power well being turned off. Make sure we restore
	 * them to a consistent state even if no primary/sprite
	 * planes are initially active.
	 */
	if (needs_modeset)
		crtc_state->fifo_changed = true;

	if (!dirty)
		return 0;

	/* cursor changes don't warrant a FIFO recompute */
	if (dirty & ~BIT(PLANE_CURSOR)) {
		const struct intel_crtc_state *old_crtc_state =
			to_intel_crtc_state(crtc->base.state);
		const struct vlv_fifo_state *old_fifo_state =
			&old_crtc_state->wm.vlv.fifo_state;

		ret = vlv_compute_fifo(crtc_state);
		if (ret)
			return ret;

		if (needs_modeset ||
		    memcmp(old_fifo_state, fifo_state,
			   sizeof(*fifo_state)) != 0)
			crtc_state->fifo_changed = true;
	}

	/* initially allow all levels */
	wm_state->num_levels = intel_wm_num_levels(dev_priv);
	/*
	 * Note that enabling cxsr with no primary/sprite planes
	 * enabled can wedge the pipe. Hence we only allow cxsr
	 * with exactly one enabled primary/sprite plane.
	 */
	wm_state->cxsr = crtc->pipe != PIPE_C && num_active_planes == 1;

	for (level = 0; level < wm_state->num_levels; level++) {
		const struct g4x_pipe_wm *raw = &crtc_state->wm.vlv.raw[level];
		const int sr_fifo_size = INTEL_INFO(dev_priv)->num_pipes * 512 - 1;

		if (!vlv_raw_crtc_wm_is_valid(crtc_state, level))
			break;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			wm_state->wm[level].plane[plane_id] =
				vlv_invert_wm_value(raw->plane[plane_id],
						    fifo_state->plane[plane_id]);
		}

		wm_state->sr[level].plane =
			vlv_invert_wm_value(max3(raw->plane[PLANE_PRIMARY],
						 raw->plane[PLANE_SPRITE0],
						 raw->plane[PLANE_SPRITE1]),
					    sr_fifo_size);

		wm_state->sr[level].cursor =
			vlv_invert_wm_value(raw->plane[PLANE_CURSOR],
					    63);
	}

	if (level == 0)
		return -EINVAL;

	/* limit to only levels we can actually handle */
	wm_state->num_levels = level;

	/* invalidate the higher levels */
	vlv_invalidate_wms(crtc, wm_state, level);

	return 0;
}

#define VLV_FIFO(plane, value) \
	(((value) << DSPARB_ ## plane ## _SHIFT_VLV) & DSPARB_ ## plane ## _MASK_VLV)

static void vlv_atomic_update_fifo(struct intel_atomic_state *state,
				   struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct vlv_fifo_state *fifo_state =
		&crtc_state->wm.vlv.fifo_state;
	int sprite0_start, sprite1_start, fifo_size;

	if (!crtc_state->fifo_changed)
		return;

	sprite0_start = fifo_state->plane[PLANE_PRIMARY];
	sprite1_start = fifo_state->plane[PLANE_SPRITE0] + sprite0_start;
	fifo_size = fifo_state->plane[PLANE_SPRITE1] + sprite1_start;

	WARN_ON(fifo_state->plane[PLANE_CURSOR] != 63);
	WARN_ON(fifo_size != 511);

	trace_vlv_fifo_size(crtc, sprite0_start, sprite1_start, fifo_size);

	/*
	 * uncore.lock serves a double purpose here. It allows us to
	 * use the less expensive I915_{READ,WRITE}_FW() functions, and
	 * it protects the DSPARB registers from getting clobbered by
	 * parallel updates from multiple pipes.
	 *
	 * intel_pipe_update_start() has already disabled interrupts
	 * for us, so a plain spin_lock() is sufficient here.
	 */
	spin_lock(&dev_priv->uncore.lock);

	switch (crtc->pipe) {
		uint32_t dsparb, dsparb2, dsparb3;
	case PIPE_A:
		dsparb = I915_READ_FW(DSPARB);
		dsparb2 = I915_READ_FW(DSPARB2);

		dsparb &= ~(VLV_FIFO(SPRITEA, 0xff) |
			    VLV_FIFO(SPRITEB, 0xff));
		dsparb |= (VLV_FIFO(SPRITEA, sprite0_start) |
			   VLV_FIFO(SPRITEB, sprite1_start));

		dsparb2 &= ~(VLV_FIFO(SPRITEA_HI, 0x1) |
			     VLV_FIFO(SPRITEB_HI, 0x1));
		dsparb2 |= (VLV_FIFO(SPRITEA_HI, sprite0_start >> 8) |
			   VLV_FIFO(SPRITEB_HI, sprite1_start >> 8));

		I915_WRITE_FW(DSPARB, dsparb);
		I915_WRITE_FW(DSPARB2, dsparb2);
		break;
	case PIPE_B:
		dsparb = I915_READ_FW(DSPARB);
		dsparb2 = I915_READ_FW(DSPARB2);

		dsparb &= ~(VLV_FIFO(SPRITEC, 0xff) |
			    VLV_FIFO(SPRITED, 0xff));
		dsparb |= (VLV_FIFO(SPRITEC, sprite0_start) |
			   VLV_FIFO(SPRITED, sprite1_start));

		dsparb2 &= ~(VLV_FIFO(SPRITEC_HI, 0xff) |
			     VLV_FIFO(SPRITED_HI, 0xff));
		dsparb2 |= (VLV_FIFO(SPRITEC_HI, sprite0_start >> 8) |
			   VLV_FIFO(SPRITED_HI, sprite1_start >> 8));

		I915_WRITE_FW(DSPARB, dsparb);
		I915_WRITE_FW(DSPARB2, dsparb2);
		break;
	case PIPE_C:
		dsparb3 = I915_READ_FW(DSPARB3);
		dsparb2 = I915_READ_FW(DSPARB2);

		dsparb3 &= ~(VLV_FIFO(SPRITEE, 0xff) |
			     VLV_FIFO(SPRITEF, 0xff));
		dsparb3 |= (VLV_FIFO(SPRITEE, sprite0_start) |
			    VLV_FIFO(SPRITEF, sprite1_start));

		dsparb2 &= ~(VLV_FIFO(SPRITEE_HI, 0xff) |
			     VLV_FIFO(SPRITEF_HI, 0xff));
		dsparb2 |= (VLV_FIFO(SPRITEE_HI, sprite0_start >> 8) |
			   VLV_FIFO(SPRITEF_HI, sprite1_start >> 8));

		I915_WRITE_FW(DSPARB3, dsparb3);
		I915_WRITE_FW(DSPARB2, dsparb2);
		break;
	default:
		break;
	}

	POSTING_READ_FW(DSPARB);

	spin_unlock(&dev_priv->uncore.lock);
}

#undef VLV_FIFO

static int vlv_compute_intermediate_wm(struct drm_device *dev,
				       struct intel_crtc *crtc,
				       struct intel_crtc_state *crtc_state)
{
	struct vlv_wm_state *intermediate = &crtc_state->wm.vlv.intermediate;
	const struct vlv_wm_state *optimal = &crtc_state->wm.vlv.optimal;
	const struct vlv_wm_state *active = &crtc->wm.active.vlv;
	int level;

	intermediate->num_levels = min(optimal->num_levels, active->num_levels);
	intermediate->cxsr = optimal->cxsr && active->cxsr &&
		!crtc_state->disable_cxsr;

	for (level = 0; level < intermediate->num_levels; level++) {
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			intermediate->wm[level].plane[plane_id] =
				min(optimal->wm[level].plane[plane_id],
				    active->wm[level].plane[plane_id]);
		}

		intermediate->sr[level].plane = min(optimal->sr[level].plane,
						    active->sr[level].plane);
		intermediate->sr[level].cursor = min(optimal->sr[level].cursor,
						     active->sr[level].cursor);
	}

	vlv_invalidate_wms(crtc, intermediate, level);

	/*
	 * If our intermediate WM are identical to the final WM, then we can
	 * omit the post-vblank programming; only update if it's different.
	 */
	if (memcmp(intermediate, optimal, sizeof(*intermediate)) != 0)
		crtc_state->wm.need_postvbl_update = true;

	return 0;
}

static void vlv_merge_wm(struct drm_i915_private *dev_priv,
			 struct vlv_wm_values *wm)
{
	struct intel_crtc *crtc;
	int num_active_crtcs = 0;

	wm->level = dev_priv->wm.max_level;
	wm->cxsr = true;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct vlv_wm_state *wm_state = &crtc->wm.active.vlv;

		if (!crtc->active)
			continue;

		if (!wm_state->cxsr)
			wm->cxsr = false;

		num_active_crtcs++;
		wm->level = min_t(int, wm->level, wm_state->num_levels - 1);
	}

	if (num_active_crtcs != 1)
		wm->cxsr = false;

	if (num_active_crtcs > 1)
		wm->level = VLV_WM_LEVEL_PM2;

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		const struct vlv_wm_state *wm_state = &crtc->wm.active.vlv;
		enum pipe pipe = crtc->pipe;

		wm->pipe[pipe] = wm_state->wm[wm->level];
		if (crtc->active && wm->cxsr)
			wm->sr = wm_state->sr[wm->level];

		wm->ddl[pipe].plane[PLANE_PRIMARY] = DDL_PRECISION_HIGH | 2;
		wm->ddl[pipe].plane[PLANE_SPRITE0] = DDL_PRECISION_HIGH | 2;
		wm->ddl[pipe].plane[PLANE_SPRITE1] = DDL_PRECISION_HIGH | 2;
		wm->ddl[pipe].plane[PLANE_CURSOR] = DDL_PRECISION_HIGH | 2;
	}
}

static void vlv_program_watermarks(struct drm_i915_private *dev_priv)
{
	struct vlv_wm_values *old_wm = &dev_priv->wm.vlv;
	struct vlv_wm_values new_wm = {};

	vlv_merge_wm(dev_priv, &new_wm);

	if (memcmp(old_wm, &new_wm, sizeof(new_wm)) == 0)
		return;

	if (is_disabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_DDR_DVFS))
		chv_set_memory_dvfs(dev_priv, false);

	if (is_disabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_PM5))
		chv_set_memory_pm5(dev_priv, false);

	if (is_disabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, false);

	vlv_write_wm_values(dev_priv, &new_wm);

	if (is_enabling(old_wm->cxsr, new_wm.cxsr, true))
		_intel_set_memory_cxsr(dev_priv, true);

	if (is_enabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_PM5))
		chv_set_memory_pm5(dev_priv, true);

	if (is_enabling(old_wm->level, new_wm.level, VLV_WM_LEVEL_DDR_DVFS))
		chv_set_memory_dvfs(dev_priv, true);

	*old_wm = new_wm;
}

static void vlv_initial_watermarks(struct intel_atomic_state *state,
				   struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);

	mutex_lock(&dev_priv->wm.wm_mutex);
	crtc->wm.active.vlv = crtc_state->wm.vlv.intermediate;
	vlv_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->wm.wm_mutex);
}

static void vlv_optimize_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->base.crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->base.crtc);

	if (!crtc_state->wm.need_postvbl_update)
		return;

	mutex_lock(&dev_priv->wm.wm_mutex);
	intel_crtc->wm.active.vlv = crtc_state->wm.vlv.optimal;
	vlv_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->wm.wm_mutex);
}

static void i965_update_wm(struct intel_crtc *unused_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(unused_crtc->base.dev);
	struct intel_crtc *crtc;
	int srwm = 1;
	int cursor_sr = 16;
	bool cxsr_enabled;

	/* Calc sr entries for one plane configs */
	crtc = single_enabled_crtc(dev_priv);
	if (crtc) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 12000;
		const struct drm_display_mode *adjusted_mode =
			&crtc->config->base.adjusted_mode;
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int clock = adjusted_mode->crtc_clock;
		int htotal = adjusted_mode->crtc_htotal;
		int hdisplay = crtc->config->pipe_src_w;
		int cpp = fb->format->cpp[0];
		int entries;

		entries = intel_wm_method2(clock, htotal,
					   hdisplay, cpp, sr_latency_ns / 100);
		entries = DIV_ROUND_UP(entries, I915_FIFO_LINE_SIZE);
		srwm = I965_FIFO_SIZE - entries;
		if (srwm < 0)
			srwm = 1;
		srwm &= 0x1ff;
		DRM_DEBUG_KMS("self-refresh entries: %d, wm: %d\n",
			      entries, srwm);

		entries = intel_wm_method2(clock, htotal,
					   crtc->base.cursor->state->crtc_w, 4,
					   sr_latency_ns / 100);
		entries = DIV_ROUND_UP(entries,
				       i965_cursor_wm_info.cacheline_size) +
			i965_cursor_wm_info.guard_size;

		cursor_sr = i965_cursor_wm_info.fifo_size - entries;
		if (cursor_sr > i965_cursor_wm_info.max_wm)
			cursor_sr = i965_cursor_wm_info.max_wm;

		DRM_DEBUG_KMS("self-refresh watermark: display plane %d "
			      "cursor %d\n", srwm, cursor_sr);

		cxsr_enabled = true;
	} else {
		cxsr_enabled = false;
		/* Turn off self refresh if both pipes are enabled */
		intel_set_memory_cxsr(dev_priv, false);
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: 8, B: 8, C: 8, SR %d\n",
		      srwm);

	/* 965 has limitations... */
	I915_WRITE(DSPFW1, FW_WM(srwm, SR) |
		   FW_WM(8, CURSORB) |
		   FW_WM(8, PLANEB) |
		   FW_WM(8, PLANEA));
	I915_WRITE(DSPFW2, FW_WM(8, CURSORA) |
		   FW_WM(8, PLANEC_OLD));
	/* update cursor SR watermark */
	I915_WRITE(DSPFW3, FW_WM(cursor_sr, CURSOR_SR));

	if (cxsr_enabled)
		intel_set_memory_cxsr(dev_priv, true);
}

#undef FW_WM

static void i9xx_update_wm(struct intel_crtc *unused_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(unused_crtc->base.dev);
	const struct intel_watermark_params *wm_info;
	uint32_t fwater_lo;
	uint32_t fwater_hi;
	int cwm, srwm = 1;
	int fifo_size;
	int planea_wm, planeb_wm;
	struct intel_crtc *crtc, *enabled = NULL;

	if (IS_I945GM(dev_priv))
		wm_info = &i945_wm_info;
	else if (!IS_GEN2(dev_priv))
		wm_info = &i915_wm_info;
	else
		wm_info = &i830_a_wm_info;

	fifo_size = dev_priv->display.get_fifo_size(dev_priv, 0);
	crtc = intel_get_crtc_for_plane(dev_priv, 0);
	if (intel_crtc_active(crtc)) {
		const struct drm_display_mode *adjusted_mode =
			&crtc->config->base.adjusted_mode;
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int cpp;

		if (IS_GEN2(dev_priv))
			cpp = 4;
		else
			cpp = fb->format->cpp[0];

		planea_wm = intel_calculate_wm(adjusted_mode->crtc_clock,
					       wm_info, fifo_size, cpp,
					       pessimal_latency_ns);
		enabled = crtc;
	} else {
		planea_wm = fifo_size - wm_info->guard_size;
		if (planea_wm > (long)wm_info->max_wm)
			planea_wm = wm_info->max_wm;
	}

	if (IS_GEN2(dev_priv))
		wm_info = &i830_bc_wm_info;

	fifo_size = dev_priv->display.get_fifo_size(dev_priv, 1);
	crtc = intel_get_crtc_for_plane(dev_priv, 1);
	if (intel_crtc_active(crtc)) {
		const struct drm_display_mode *adjusted_mode =
			&crtc->config->base.adjusted_mode;
		const struct drm_framebuffer *fb =
			crtc->base.primary->state->fb;
		int cpp;

		if (IS_GEN2(dev_priv))
			cpp = 4;
		else
			cpp = fb->format->cpp[0];

		planeb_wm = intel_calculate_wm(adjusted_mode->crtc_clock,
					       wm_info, fifo_size, cpp,
					       pessimal_latency_ns);
		if (enabled == NULL)
			enabled = crtc;
		else
			enabled = NULL;
	} else {
		planeb_wm = fifo_size - wm_info->guard_size;
		if (planeb_wm > (long)wm_info->max_wm)
			planeb_wm = wm_info->max_wm;
	}

	DRM_DEBUG_KMS("FIFO watermarks - A: %d, B: %d\n", planea_wm, planeb_wm);

	if (IS_I915GM(dev_priv) && enabled) {
		struct drm_i915_gem_object *obj;

		obj = intel_fb_obj(enabled->base.primary->state->fb);

		/* self-refresh seems busted with untiled */
		if (!i915_gem_object_is_tiled(obj))
			enabled = NULL;
	}

	/*
	 * Overlay gets an aggressive default since video jitter is bad.
	 */
	cwm = 2;

	/* Play safe and disable self-refresh before adjusting watermarks. */
	intel_set_memory_cxsr(dev_priv, false);

	/* Calc sr entries for one plane configs */
	if (HAS_FW_BLC(dev_priv) && enabled) {
		/* self-refresh has much higher latency */
		static const int sr_latency_ns = 6000;
		const struct drm_display_mode *adjusted_mode =
			&enabled->config->base.adjusted_mode;
		const struct drm_framebuffer *fb =
			enabled->base.primary->state->fb;
		int clock = adjusted_mode->crtc_clock;
		int htotal = adjusted_mode->crtc_htotal;
		int hdisplay = enabled->config->pipe_src_w;
		int cpp;
		int entries;

		if (IS_I915GM(dev_priv) || IS_I945GM(dev_priv))
			cpp = 4;
		else
			cpp = fb->format->cpp[0];

		entries = intel_wm_method2(clock, htotal, hdisplay, cpp,
					   sr_latency_ns / 100);
		entries = DIV_ROUND_UP(entries, wm_info->cacheline_size);
		DRM_DEBUG_KMS("self-refresh entries: %d\n", entries);
		srwm = wm_info->fifo_size - entries;
		if (srwm < 0)
			srwm = 1;

		if (IS_I945G(dev_priv) || IS_I945GM(dev_priv))
			I915_WRITE(FW_BLC_SELF,
				   FW_BLC_SELF_FIFO_MASK | (srwm & 0xff));
		else
			I915_WRITE(FW_BLC_SELF, srwm & 0x3f);
	}

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: %d, B: %d, C: %d, SR %d\n",
		      planea_wm, planeb_wm, cwm, srwm);

	fwater_lo = ((planeb_wm & 0x3f) << 16) | (planea_wm & 0x3f);
	fwater_hi = (cwm & 0x1f);

	/* Set request length to 8 cachelines per fetch */
	fwater_lo = fwater_lo | (1 << 24) | (1 << 8);
	fwater_hi = fwater_hi | (1 << 8);

	I915_WRITE(FW_BLC, fwater_lo);
	I915_WRITE(FW_BLC2, fwater_hi);

	if (enabled)
		intel_set_memory_cxsr(dev_priv, true);
}

static void i845_update_wm(struct intel_crtc *unused_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(unused_crtc->base.dev);
	struct intel_crtc *crtc;
	const struct drm_display_mode *adjusted_mode;
	uint32_t fwater_lo;
	int planea_wm;

	crtc = single_enabled_crtc(dev_priv);
	if (crtc == NULL)
		return;

	adjusted_mode = &crtc->config->base.adjusted_mode;
	planea_wm = intel_calculate_wm(adjusted_mode->crtc_clock,
				       &i845_wm_info,
				       dev_priv->display.get_fifo_size(dev_priv, 0),
				       4, pessimal_latency_ns);
	fwater_lo = I915_READ(FW_BLC) & ~0xfff;
	fwater_lo |= (3<<8) | planea_wm;

	DRM_DEBUG_KMS("Setting FIFO watermarks - A: %d\n", planea_wm);

	I915_WRITE(FW_BLC, fwater_lo);
}

/* latency must be in 0.1us units. */
static unsigned int ilk_wm_method1(unsigned int pixel_rate,
				   unsigned int cpp,
				   unsigned int latency)
{
	unsigned int ret;

	ret = intel_wm_method1(pixel_rate, cpp, latency);
	ret = DIV_ROUND_UP(ret, 64) + 2;

	return ret;
}

/* latency must be in 0.1us units. */
static unsigned int ilk_wm_method2(unsigned int pixel_rate,
				   unsigned int htotal,
				   unsigned int width,
				   unsigned int cpp,
				   unsigned int latency)
{
	unsigned int ret;

	ret = intel_wm_method2(pixel_rate, htotal,
			       width, cpp, latency);
	ret = DIV_ROUND_UP(ret, 64) + 2;

	return ret;
}

static uint32_t ilk_wm_fbc(uint32_t pri_val, uint32_t horiz_pixels,
			   uint8_t cpp)
{
	/*
	 * Neither of these should be possible since this function shouldn't be
	 * called if the CRTC is off or the plane is invisible.  But let's be
	 * extra paranoid to avoid a potential divide-by-zero if we screw up
	 * elsewhere in the driver.
	 */
	if (WARN_ON(!cpp))
		return 0;
	if (WARN_ON(!horiz_pixels))
		return 0;

	return DIV_ROUND_UP(pri_val * 64, horiz_pixels * cpp) + 2;
}

struct ilk_wm_maximums {
	uint16_t pri;
	uint16_t spr;
	uint16_t cur;
	uint16_t fbc;
};

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static uint32_t ilk_compute_pri_wm(const struct intel_crtc_state *cstate,
				   const struct intel_plane_state *pstate,
				   uint32_t mem_value,
				   bool is_lp)
{
	uint32_t method1, method2;
	int cpp;

	if (!intel_wm_plane_visible(cstate, pstate))
		return 0;

	cpp = pstate->base.fb->format->cpp[0];

	method1 = ilk_wm_method1(cstate->pixel_rate, cpp, mem_value);

	if (!is_lp)
		return method1;

	method2 = ilk_wm_method2(cstate->pixel_rate,
				 cstate->base.adjusted_mode.crtc_htotal,
				 drm_rect_width(&pstate->base.dst),
				 cpp, mem_value);

	return min(method1, method2);
}

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static uint32_t ilk_compute_spr_wm(const struct intel_crtc_state *cstate,
				   const struct intel_plane_state *pstate,
				   uint32_t mem_value)
{
	uint32_t method1, method2;
	int cpp;

	if (!intel_wm_plane_visible(cstate, pstate))
		return 0;

	cpp = pstate->base.fb->format->cpp[0];

	method1 = ilk_wm_method1(cstate->pixel_rate, cpp, mem_value);
	method2 = ilk_wm_method2(cstate->pixel_rate,
				 cstate->base.adjusted_mode.crtc_htotal,
				 drm_rect_width(&pstate->base.dst),
				 cpp, mem_value);
	return min(method1, method2);
}

/*
 * For both WM_PIPE and WM_LP.
 * mem_value must be in 0.1us units.
 */
static uint32_t ilk_compute_cur_wm(const struct intel_crtc_state *cstate,
				   const struct intel_plane_state *pstate,
				   uint32_t mem_value)
{
	int cpp;

	if (!intel_wm_plane_visible(cstate, pstate))
		return 0;

	cpp = pstate->base.fb->format->cpp[0];

	return ilk_wm_method2(cstate->pixel_rate,
			      cstate->base.adjusted_mode.crtc_htotal,
			      pstate->base.crtc_w, cpp, mem_value);
}

/* Only for WM_LP. */
static uint32_t ilk_compute_fbc_wm(const struct intel_crtc_state *cstate,
				   const struct intel_plane_state *pstate,
				   uint32_t pri_val)
{
	int cpp;

	if (!intel_wm_plane_visible(cstate, pstate))
		return 0;

	cpp = pstate->base.fb->format->cpp[0];

	return ilk_wm_fbc(pri_val, drm_rect_width(&pstate->base.dst), cpp);
}

static unsigned int
ilk_display_fifo_size(const struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) >= 8)
		return 3072;
	else if (INTEL_GEN(dev_priv) >= 7)
		return 768;
	else
		return 512;
}

static unsigned int
ilk_plane_wm_reg_max(const struct drm_i915_private *dev_priv,
		     int level, bool is_sprite)
{
	if (INTEL_GEN(dev_priv) >= 8)
		/* BDW primary/sprite plane watermarks */
		return level == 0 ? 255 : 2047;
	else if (INTEL_GEN(dev_priv) >= 7)
		/* IVB/HSW primary/sprite plane watermarks */
		return level == 0 ? 127 : 1023;
	else if (!is_sprite)
		/* ILK/SNB primary plane watermarks */
		return level == 0 ? 127 : 511;
	else
		/* ILK/SNB sprite plane watermarks */
		return level == 0 ? 63 : 255;
}

static unsigned int
ilk_cursor_wm_reg_max(const struct drm_i915_private *dev_priv, int level)
{
	if (INTEL_GEN(dev_priv) >= 7)
		return level == 0 ? 63 : 255;
	else
		return level == 0 ? 31 : 63;
}

static unsigned int ilk_fbc_wm_reg_max(const struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) >= 8)
		return 31;
	else
		return 15;
}

/* Calculate the maximum primary/sprite plane watermark */
static unsigned int ilk_plane_wm_max(const struct drm_device *dev,
				     int level,
				     const struct intel_wm_config *config,
				     enum intel_ddb_partitioning ddb_partitioning,
				     bool is_sprite)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned int fifo_size = ilk_display_fifo_size(dev_priv);

	/* if sprites aren't enabled, sprites get nothing */
	if (is_sprite && !config->sprites_enabled)
		return 0;

	/* HSW allows LP1+ watermarks even with multiple pipes */
	if (level == 0 || config->num_pipes_active > 1) {
		fifo_size /= INTEL_INFO(dev_priv)->num_pipes;

		/*
		 * For some reason the non self refresh
		 * FIFO size is only half of the self
		 * refresh FIFO size on ILK/SNB.
		 */
		if (INTEL_GEN(dev_priv) <= 6)
			fifo_size /= 2;
	}

	if (config->sprites_enabled) {
		/* level 0 is always calculated with 1:1 split */
		if (level > 0 && ddb_partitioning == INTEL_DDB_PART_5_6) {
			if (is_sprite)
				fifo_size *= 5;
			fifo_size /= 6;
		} else {
			fifo_size /= 2;
		}
	}

	/* clamp to max that the registers can hold */
	return min(fifo_size, ilk_plane_wm_reg_max(dev_priv, level, is_sprite));
}

/* Calculate the maximum cursor plane watermark */
static unsigned int ilk_cursor_wm_max(const struct drm_device *dev,
				      int level,
				      const struct intel_wm_config *config)
{
	/* HSW LP1+ watermarks w/ multiple pipes */
	if (level > 0 && config->num_pipes_active > 1)
		return 64;

	/* otherwise just report max that registers can hold */
	return ilk_cursor_wm_reg_max(to_i915(dev), level);
}

static void ilk_compute_wm_maximums(const struct drm_device *dev,
				    int level,
				    const struct intel_wm_config *config,
				    enum intel_ddb_partitioning ddb_partitioning,
				    struct ilk_wm_maximums *max)
{
	max->pri = ilk_plane_wm_max(dev, level, config, ddb_partitioning, false);
	max->spr = ilk_plane_wm_max(dev, level, config, ddb_partitioning, true);
	max->cur = ilk_cursor_wm_max(dev, level, config);
	max->fbc = ilk_fbc_wm_reg_max(to_i915(dev));
}

static void ilk_compute_wm_reg_maximums(const struct drm_i915_private *dev_priv,
					int level,
					struct ilk_wm_maximums *max)
{
	max->pri = ilk_plane_wm_reg_max(dev_priv, level, false);
	max->spr = ilk_plane_wm_reg_max(dev_priv, level, true);
	max->cur = ilk_cursor_wm_reg_max(dev_priv, level);
	max->fbc = ilk_fbc_wm_reg_max(dev_priv);
}

static bool ilk_validate_wm_level(int level,
				  const struct ilk_wm_maximums *max,
				  struct intel_wm_level *result)
{
	bool ret;

	/* already determined to be invalid? */
	if (!result->enable)
		return false;

	result->enable = result->pri_val <= max->pri &&
			 result->spr_val <= max->spr &&
			 result->cur_val <= max->cur;

	ret = result->enable;

	/*
	 * HACK until we can pre-compute everything,
	 * and thus fail gracefully if LP0 watermarks
	 * are exceeded...
	 */
	if (level == 0 && !result->enable) {
		if (result->pri_val > max->pri)
			DRM_DEBUG_KMS("Primary WM%d too large %u (max %u)\n",
				      level, result->pri_val, max->pri);
		if (result->spr_val > max->spr)
			DRM_DEBUG_KMS("Sprite WM%d too large %u (max %u)\n",
				      level, result->spr_val, max->spr);
		if (result->cur_val > max->cur)
			DRM_DEBUG_KMS("Cursor WM%d too large %u (max %u)\n",
				      level, result->cur_val, max->cur);

		result->pri_val = min_t(uint32_t, result->pri_val, max->pri);
		result->spr_val = min_t(uint32_t, result->spr_val, max->spr);
		result->cur_val = min_t(uint32_t, result->cur_val, max->cur);
		result->enable = true;
	}

	return ret;
}

static void ilk_compute_wm_level(const struct drm_i915_private *dev_priv,
				 const struct intel_crtc *intel_crtc,
				 int level,
				 struct intel_crtc_state *cstate,
				 struct intel_plane_state *pristate,
				 struct intel_plane_state *sprstate,
				 struct intel_plane_state *curstate,
				 struct intel_wm_level *result)
{
	uint16_t pri_latency = dev_priv->wm.pri_latency[level];
	uint16_t spr_latency = dev_priv->wm.spr_latency[level];
	uint16_t cur_latency = dev_priv->wm.cur_latency[level];

	/* WM1+ latency values stored in 0.5us units */
	if (level > 0) {
		pri_latency *= 5;
		spr_latency *= 5;
		cur_latency *= 5;
	}

	if (pristate) {
		result->pri_val = ilk_compute_pri_wm(cstate, pristate,
						     pri_latency, level);
		result->fbc_val = ilk_compute_fbc_wm(cstate, pristate, result->pri_val);
	}

	if (sprstate)
		result->spr_val = ilk_compute_spr_wm(cstate, sprstate, spr_latency);

	if (curstate)
		result->cur_val = ilk_compute_cur_wm(cstate, curstate, cur_latency);

	result->enable = true;
}

static uint32_t
hsw_compute_linetime_wm(const struct intel_crtc_state *cstate)
{
	const struct intel_atomic_state *intel_state =
		to_intel_atomic_state(cstate->base.state);
	const struct drm_display_mode *adjusted_mode =
		&cstate->base.adjusted_mode;
	u32 linetime, ips_linetime;

	if (!cstate->base.active)
		return 0;
	if (WARN_ON(adjusted_mode->crtc_clock == 0))
		return 0;
	if (WARN_ON(intel_state->cdclk.logical.cdclk == 0))
		return 0;

	/* The WM are computed with base on how long it takes to fill a single
	 * row at the given clock rate, multiplied by 8.
	 * */
	linetime = DIV_ROUND_CLOSEST(adjusted_mode->crtc_htotal * 1000 * 8,
				     adjusted_mode->crtc_clock);
	ips_linetime = DIV_ROUND_CLOSEST(adjusted_mode->crtc_htotal * 1000 * 8,
					 intel_state->cdclk.logical.cdclk);

	return PIPE_WM_LINETIME_IPS_LINETIME(ips_linetime) |
	       PIPE_WM_LINETIME_TIME(linetime);
}

static void intel_read_wm_latency(struct drm_i915_private *dev_priv,
				  uint16_t wm[8])
{
	if (INTEL_GEN(dev_priv) >= 9) {
		uint32_t val;
		int ret, i;
		int level, max_level = ilk_wm_max_level(dev_priv);

		/* read the first set of memory latencies[0:3] */
		val = 0; /* data0 to be programmed to 0 for first set */
		mutex_lock(&dev_priv->rps.hw_lock);
		ret = sandybridge_pcode_read(dev_priv,
					     GEN9_PCODE_READ_MEM_LATENCY,
					     &val);
		mutex_unlock(&dev_priv->rps.hw_lock);

		if (ret) {
			DRM_ERROR("SKL Mailbox read error = %d\n", ret);
			return;
		}

		wm[0] = val & GEN9_MEM_LATENCY_LEVEL_MASK;
		wm[1] = (val >> GEN9_MEM_LATENCY_LEVEL_1_5_SHIFT) &
				GEN9_MEM_LATENCY_LEVEL_MASK;
		wm[2] = (val >> GEN9_MEM_LATENCY_LEVEL_2_6_SHIFT) &
				GEN9_MEM_LATENCY_LEVEL_MASK;
		wm[3] = (val >> GEN9_MEM_LATENCY_LEVEL_3_7_SHIFT) &
				GEN9_MEM_LATENCY_LEVEL_MASK;

		/* read the second set of memory latencies[4:7] */
		val = 1; /* data0 to be programmed to 1 for second set */
		mutex_lock(&dev_priv->rps.hw_lock);
		ret = sandybridge_pcode_read(dev_priv,
					     GEN9_PCODE_READ_MEM_LATENCY,
					     &val);
		mutex_unlock(&dev_priv->rps.hw_lock);
		if (ret) {
			DRM_ERROR("SKL Mailbox read error = %d\n", ret);
			return;
		}

		wm[4] = val & GEN9_MEM_LATENCY_LEVEL_MASK;
		wm[5] = (val >> GEN9_MEM_LATENCY_LEVEL_1_5_SHIFT) &
				GEN9_MEM_LATENCY_LEVEL_MASK;
		wm[6] = (val >> GEN9_MEM_LATENCY_LEVEL_2_6_SHIFT) &
				GEN9_MEM_LATENCY_LEVEL_MASK;
		wm[7] = (val >> GEN9_MEM_LATENCY_LEVEL_3_7_SHIFT) &
				GEN9_MEM_LATENCY_LEVEL_MASK;

		/*
		 * If a level n (n > 1) has a 0us latency, all levels m (m >= n)
		 * need to be disabled. We make sure to sanitize the values out
		 * of the punit to satisfy this requirement.
		 */
		for (level = 1; level <= max_level; level++) {
			if (wm[level] == 0) {
				for (i = level + 1; i <= max_level; i++)
					wm[i] = 0;
				break;
			}
		}

		/*
		 * WaWmMemoryReadLatency:skl+,glk
		 *
		 * punit doesn't take into account the read latency so we need
		 * to add 2us to the various latency levels we retrieve from the
		 * punit when level 0 response data us 0us.
		 */
		if (wm[0] == 0) {
			wm[0] += 2;
			for (level = 1; level <= max_level; level++) {
				if (wm[level] == 0)
					break;
				wm[level] += 2;
			}
		}

	} else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		uint64_t sskpd = I915_READ64(MCH_SSKPD);

		wm[0] = (sskpd >> 56) & 0xFF;
		if (wm[0] == 0)
			wm[0] = sskpd & 0xF;
		wm[1] = (sskpd >> 4) & 0xFF;
		wm[2] = (sskpd >> 12) & 0xFF;
		wm[3] = (sskpd >> 20) & 0x1FF;
		wm[4] = (sskpd >> 32) & 0x1FF;
	} else if (INTEL_GEN(dev_priv) >= 6) {
		uint32_t sskpd = I915_READ(MCH_SSKPD);

		wm[0] = (sskpd >> SSKPD_WM0_SHIFT) & SSKPD_WM_MASK;
		wm[1] = (sskpd >> SSKPD_WM1_SHIFT) & SSKPD_WM_MASK;
		wm[2] = (sskpd >> SSKPD_WM2_SHIFT) & SSKPD_WM_MASK;
		wm[3] = (sskpd >> SSKPD_WM3_SHIFT) & SSKPD_WM_MASK;
	} else if (INTEL_GEN(dev_priv) >= 5) {
		uint32_t mltr = I915_READ(MLTR_ILK);

		/* ILK primary LP0 latency is 700 ns */
		wm[0] = 7;
		wm[1] = (mltr >> MLTR_WM1_SHIFT) & ILK_SRLT_MASK;
		wm[2] = (mltr >> MLTR_WM2_SHIFT) & ILK_SRLT_MASK;
	} else {
		MISSING_CASE(INTEL_DEVID(dev_priv));
	}
}

static void intel_fixup_spr_wm_latency(struct drm_i915_private *dev_priv,
				       uint16_t wm[5])
{
	/* ILK sprite LP0 latency is 1300 ns */
	if (IS_GEN5(dev_priv))
		wm[0] = 13;
}

static void intel_fixup_cur_wm_latency(struct drm_i915_private *dev_priv,
				       uint16_t wm[5])
{
	/* ILK cursor LP0 latency is 1300 ns */
	if (IS_GEN5(dev_priv))
		wm[0] = 13;

	/* WaDoubleCursorLP3Latency:ivb */
	if (IS_IVYBRIDGE(dev_priv))
		wm[3] *= 2;
}

int ilk_wm_max_level(const struct drm_i915_private *dev_priv)
{
	/* how many WM levels are we expecting */
	if (INTEL_GEN(dev_priv) >= 9)
		return 7;
	else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		return 4;
	else if (INTEL_GEN(dev_priv) >= 6)
		return 3;
	else
		return 2;
}

static void intel_print_wm_latency(struct drm_i915_private *dev_priv,
				   const char *name,
				   const uint16_t wm[8])
{
	int level, max_level = ilk_wm_max_level(dev_priv);

	for (level = 0; level <= max_level; level++) {
		unsigned int latency = wm[level];

		if (latency == 0) {
			DRM_ERROR("%s WM%d latency not provided\n",
				  name, level);
			continue;
		}

		/*
		 * - latencies are in us on gen9.
		 * - before then, WM1+ latency values are in 0.5us units
		 */
		if (INTEL_GEN(dev_priv) >= 9)
			latency *= 10;
		else if (level > 0)
			latency *= 5;

		DRM_DEBUG_KMS("%s WM%d latency %u (%u.%u usec)\n",
			      name, level, wm[level],
			      latency / 10, latency % 10);
	}
}

static bool ilk_increase_wm_latency(struct drm_i915_private *dev_priv,
				    uint16_t wm[5], uint16_t min)
{
	int level, max_level = ilk_wm_max_level(dev_priv);

	if (wm[0] >= min)
		return false;

	wm[0] = max(wm[0], min);
	for (level = 1; level <= max_level; level++)
		wm[level] = max_t(uint16_t, wm[level], DIV_ROUND_UP(min, 5));

	return true;
}

static void snb_wm_latency_quirk(struct drm_i915_private *dev_priv)
{
	bool changed;

	/*
	 * The BIOS provided WM memory latency values are often
	 * inadequate for high resolution displays. Adjust them.
	 */
	changed = ilk_increase_wm_latency(dev_priv, dev_priv->wm.pri_latency, 12) |
		ilk_increase_wm_latency(dev_priv, dev_priv->wm.spr_latency, 12) |
		ilk_increase_wm_latency(dev_priv, dev_priv->wm.cur_latency, 12);

	if (!changed)
		return;

	DRM_DEBUG_KMS("WM latency values increased to avoid potential underruns\n");
	intel_print_wm_latency(dev_priv, "Primary", dev_priv->wm.pri_latency);
	intel_print_wm_latency(dev_priv, "Sprite", dev_priv->wm.spr_latency);
	intel_print_wm_latency(dev_priv, "Cursor", dev_priv->wm.cur_latency);
}

static void ilk_setup_wm_latency(struct drm_i915_private *dev_priv)
{
	intel_read_wm_latency(dev_priv, dev_priv->wm.pri_latency);

	memcpy(dev_priv->wm.spr_latency, dev_priv->wm.pri_latency,
	       sizeof(dev_priv->wm.pri_latency));
	memcpy(dev_priv->wm.cur_latency, dev_priv->wm.pri_latency,
	       sizeof(dev_priv->wm.pri_latency));

	intel_fixup_spr_wm_latency(dev_priv, dev_priv->wm.spr_latency);
	intel_fixup_cur_wm_latency(dev_priv, dev_priv->wm.cur_latency);

	intel_print_wm_latency(dev_priv, "Primary", dev_priv->wm.pri_latency);
	intel_print_wm_latency(dev_priv, "Sprite", dev_priv->wm.spr_latency);
	intel_print_wm_latency(dev_priv, "Cursor", dev_priv->wm.cur_latency);

	if (IS_GEN6(dev_priv))
		snb_wm_latency_quirk(dev_priv);
}

static void skl_setup_wm_latency(struct drm_i915_private *dev_priv)
{
	intel_read_wm_latency(dev_priv, dev_priv->wm.skl_latency);
	intel_print_wm_latency(dev_priv, "Gen9 Plane", dev_priv->wm.skl_latency);
}

static bool ilk_validate_pipe_wm(struct drm_device *dev,
				 struct intel_pipe_wm *pipe_wm)
{
	/* LP0 watermark maximums depend on this pipe alone */
	const struct intel_wm_config config = {
		.num_pipes_active = 1,
		.sprites_enabled = pipe_wm->sprites_enabled,
		.sprites_scaled = pipe_wm->sprites_scaled,
	};
	struct ilk_wm_maximums max;

	/* LP0 watermarks always use 1/2 DDB partitioning */
	ilk_compute_wm_maximums(dev, 0, &config, INTEL_DDB_PART_1_2, &max);

	/* At least LP0 must be valid */
	if (!ilk_validate_wm_level(0, &max, &pipe_wm->wm[0])) {
		DRM_DEBUG_KMS("LP0 watermark invalid\n");
		return false;
	}

	return true;
}

/* Compute new watermarks for the pipe */
static int ilk_compute_pipe_wm(struct intel_crtc_state *cstate)
{
	struct drm_atomic_state *state = cstate->base.state;
	struct intel_crtc *intel_crtc = to_intel_crtc(cstate->base.crtc);
	struct intel_pipe_wm *pipe_wm;
	struct drm_device *dev = state->dev;
	const struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_plane *intel_plane;
	struct intel_plane_state *pristate = NULL;
	struct intel_plane_state *sprstate = NULL;
	struct intel_plane_state *curstate = NULL;
	int level, max_level = ilk_wm_max_level(dev_priv), usable_level;
	struct ilk_wm_maximums max;

	pipe_wm = &cstate->wm.ilk.optimal;

	for_each_intel_plane_on_crtc(dev, intel_crtc, intel_plane) {
		struct intel_plane_state *ps;

		ps = intel_atomic_get_existing_plane_state(state,
							   intel_plane);
		if (!ps)
			continue;

		if (intel_plane->base.type == DRM_PLANE_TYPE_PRIMARY)
			pristate = ps;
		else if (intel_plane->base.type == DRM_PLANE_TYPE_OVERLAY)
			sprstate = ps;
		else if (intel_plane->base.type == DRM_PLANE_TYPE_CURSOR)
			curstate = ps;
	}

	pipe_wm->pipe_enabled = cstate->base.active;
	if (sprstate) {
		pipe_wm->sprites_enabled = sprstate->base.visible;
		pipe_wm->sprites_scaled = sprstate->base.visible &&
			(drm_rect_width(&sprstate->base.dst) != drm_rect_width(&sprstate->base.src) >> 16 ||
			 drm_rect_height(&sprstate->base.dst) != drm_rect_height(&sprstate->base.src) >> 16);
	}

	usable_level = max_level;

	/* ILK/SNB: LP2+ watermarks only w/o sprites */
	if (INTEL_GEN(dev_priv) <= 6 && pipe_wm->sprites_enabled)
		usable_level = 1;

	/* ILK/SNB/IVB: LP1+ watermarks only w/o scaling */
	if (pipe_wm->sprites_scaled)
		usable_level = 0;

	ilk_compute_wm_level(dev_priv, intel_crtc, 0, cstate,
			     pristate, sprstate, curstate, &pipe_wm->raw_wm[0]);

	memset(&pipe_wm->wm, 0, sizeof(pipe_wm->wm));
	pipe_wm->wm[0] = pipe_wm->raw_wm[0];

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		pipe_wm->linetime = hsw_compute_linetime_wm(cstate);

	if (!ilk_validate_pipe_wm(dev, pipe_wm))
		return -EINVAL;

	ilk_compute_wm_reg_maximums(dev_priv, 1, &max);

	for (level = 1; level <= max_level; level++) {
		struct intel_wm_level *wm = &pipe_wm->raw_wm[level];

		ilk_compute_wm_level(dev_priv, intel_crtc, level, cstate,
				     pristate, sprstate, curstate, wm);

		/*
		 * Disable any watermark level that exceeds the
		 * register maximums since such watermarks are
		 * always invalid.
		 */
		if (level > usable_level)
			continue;

		if (ilk_validate_wm_level(level, &max, wm))
			pipe_wm->wm[level] = *wm;
		else
			usable_level = level;
	}

	return 0;
}

/*
 * Build a set of 'intermediate' watermark values that satisfy both the old
 * state and the new state.  These can be programmed to the hardware
 * immediately.
 */
static int ilk_compute_intermediate_wm(struct drm_device *dev,
				       struct intel_crtc *intel_crtc,
				       struct intel_crtc_state *newstate)
{
	struct intel_pipe_wm *a = &newstate->wm.ilk.intermediate;
	struct intel_pipe_wm *b = &intel_crtc->wm.active.ilk;
	int level, max_level = ilk_wm_max_level(to_i915(dev));

	/*
	 * Start with the final, target watermarks, then combine with the
	 * currently active watermarks to get values that are safe both before
	 * and after the vblank.
	 */
	*a = newstate->wm.ilk.optimal;
	a->pipe_enabled |= b->pipe_enabled;
	a->sprites_enabled |= b->sprites_enabled;
	a->sprites_scaled |= b->sprites_scaled;

	for (level = 0; level <= max_level; level++) {
		struct intel_wm_level *a_wm = &a->wm[level];
		const struct intel_wm_level *b_wm = &b->wm[level];

		a_wm->enable &= b_wm->enable;
		a_wm->pri_val = max(a_wm->pri_val, b_wm->pri_val);
		a_wm->spr_val = max(a_wm->spr_val, b_wm->spr_val);
		a_wm->cur_val = max(a_wm->cur_val, b_wm->cur_val);
		a_wm->fbc_val = max(a_wm->fbc_val, b_wm->fbc_val);
	}

	/*
	 * We need to make sure that these merged watermark values are
	 * actually a valid configuration themselves.  If they're not,
	 * there's no safe way to transition from the old state to
	 * the new state, so we need to fail the atomic transaction.
	 */
	if (!ilk_validate_pipe_wm(dev, a))
		return -EINVAL;

	/*
	 * If our intermediate WM are identical to the final WM, then we can
	 * omit the post-vblank programming; only update if it's different.
	 */
	if (memcmp(a, &newstate->wm.ilk.optimal, sizeof(*a)) != 0)
		newstate->wm.need_postvbl_update = true;

	return 0;
}

/*
 * Merge the watermarks from all active pipes for a specific level.
 */
static void ilk_merge_wm_level(struct drm_device *dev,
			       int level,
			       struct intel_wm_level *ret_wm)
{
	const struct intel_crtc *intel_crtc;

	ret_wm->enable = true;

	for_each_intel_crtc(dev, intel_crtc) {
		const struct intel_pipe_wm *active = &intel_crtc->wm.active.ilk;
		const struct intel_wm_level *wm = &active->wm[level];

		if (!active->pipe_enabled)
			continue;

		/*
		 * The watermark values may have been used in the past,
		 * so we must maintain them in the registers for some
		 * time even if the level is now disabled.
		 */
		if (!wm->enable)
			ret_wm->enable = false;

		ret_wm->pri_val = max(ret_wm->pri_val, wm->pri_val);
		ret_wm->spr_val = max(ret_wm->spr_val, wm->spr_val);
		ret_wm->cur_val = max(ret_wm->cur_val, wm->cur_val);
		ret_wm->fbc_val = max(ret_wm->fbc_val, wm->fbc_val);
	}
}

/*
 * Merge all low power watermarks for all active pipes.
 */
static void ilk_wm_merge(struct drm_device *dev,
			 const struct intel_wm_config *config,
			 const struct ilk_wm_maximums *max,
			 struct intel_pipe_wm *merged)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int level, max_level = ilk_wm_max_level(dev_priv);
	int last_enabled_level = max_level;

	/* ILK/SNB/IVB: LP1+ watermarks only w/ single pipe */
	if ((INTEL_GEN(dev_priv) <= 6 || IS_IVYBRIDGE(dev_priv)) &&
	    config->num_pipes_active > 1)
		last_enabled_level = 0;

	/* ILK: FBC WM must be disabled always */
	merged->fbc_wm_enabled = INTEL_GEN(dev_priv) >= 6;

	/* merge each WM1+ level */
	for (level = 1; level <= max_level; level++) {
		struct intel_wm_level *wm = &merged->wm[level];

		ilk_merge_wm_level(dev, level, wm);

		if (level > last_enabled_level)
			wm->enable = false;
		else if (!ilk_validate_wm_level(level, max, wm))
			/* make sure all following levels get disabled */
			last_enabled_level = level - 1;

		/*
		 * The spec says it is preferred to disable
		 * FBC WMs instead of disabling a WM level.
		 */
		if (wm->fbc_val > max->fbc) {
			if (wm->enable)
				merged->fbc_wm_enabled = false;
			wm->fbc_val = 0;
		}
	}

	/* ILK: LP2+ must be disabled when FBC WM is disabled but FBC enabled */
	/*
	 * FIXME this is racy. FBC might get enabled later.
	 * What we should check here is whether FBC can be
	 * enabled sometime later.
	 */
	if (IS_GEN5(dev_priv) && !merged->fbc_wm_enabled &&
	    intel_fbc_is_active(dev_priv)) {
		for (level = 2; level <= max_level; level++) {
			struct intel_wm_level *wm = &merged->wm[level];

			wm->enable = false;
		}
	}
}

static int ilk_wm_lp_to_level(int wm_lp, const struct intel_pipe_wm *pipe_wm)
{
	/* LP1,LP2,LP3 levels are either 1,2,3 or 1,3,4 */
	return wm_lp + (wm_lp >= 2 && pipe_wm->wm[4].enable);
}

/* The value we need to program into the WM_LPx latency field */
static unsigned int ilk_wm_lp_latency(struct drm_device *dev, int level)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		return 2 * level;
	else
		return dev_priv->wm.pri_latency[level];
}

static void ilk_compute_wm_results(struct drm_device *dev,
				   const struct intel_pipe_wm *merged,
				   enum intel_ddb_partitioning partitioning,
				   struct ilk_wm_values *results)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_crtc *intel_crtc;
	int level, wm_lp;

	results->enable_fbc_wm = merged->fbc_wm_enabled;
	results->partitioning = partitioning;

	/* LP1+ register values */
	for (wm_lp = 1; wm_lp <= 3; wm_lp++) {
		const struct intel_wm_level *r;

		level = ilk_wm_lp_to_level(wm_lp, merged);

		r = &merged->wm[level];

		/*
		 * Maintain the watermark values even if the level is
		 * disabled. Doing otherwise could cause underruns.
		 */
		results->wm_lp[wm_lp - 1] =
			(ilk_wm_lp_latency(dev, level) << WM1_LP_LATENCY_SHIFT) |
			(r->pri_val << WM1_LP_SR_SHIFT) |
			r->cur_val;

		if (r->enable)
			results->wm_lp[wm_lp - 1] |= WM1_LP_SR_EN;

		if (INTEL_GEN(dev_priv) >= 8)
			results->wm_lp[wm_lp - 1] |=
				r->fbc_val << WM1_LP_FBC_SHIFT_BDW;
		else
			results->wm_lp[wm_lp - 1] |=
				r->fbc_val << WM1_LP_FBC_SHIFT;

		/*
		 * Always set WM1S_LP_EN when spr_val != 0, even if the
		 * level is disabled. Doing otherwise could cause underruns.
		 */
		if (INTEL_GEN(dev_priv) <= 6 && r->spr_val) {
			WARN_ON(wm_lp != 1);
			results->wm_lp_spr[wm_lp - 1] = WM1S_LP_EN | r->spr_val;
		} else
			results->wm_lp_spr[wm_lp - 1] = r->spr_val;
	}

	/* LP0 register values */
	for_each_intel_crtc(dev, intel_crtc) {
		enum pipe pipe = intel_crtc->pipe;
		const struct intel_wm_level *r =
			&intel_crtc->wm.active.ilk.wm[0];

		if (WARN_ON(!r->enable))
			continue;

		results->wm_linetime[pipe] = intel_crtc->wm.active.ilk.linetime;

		results->wm_pipe[pipe] =
			(r->pri_val << WM0_PIPE_PLANE_SHIFT) |
			(r->spr_val << WM0_PIPE_SPRITE_SHIFT) |
			r->cur_val;
	}
}

/* Find the result with the highest level enabled. Check for enable_fbc_wm in
 * case both are at the same level. Prefer r1 in case they're the same. */
static struct intel_pipe_wm *ilk_find_best_result(struct drm_device *dev,
						  struct intel_pipe_wm *r1,
						  struct intel_pipe_wm *r2)
{
	int level, max_level = ilk_wm_max_level(to_i915(dev));
	int level1 = 0, level2 = 0;

	for (level = 1; level <= max_level; level++) {
		if (r1->wm[level].enable)
			level1 = level;
		if (r2->wm[level].enable)
			level2 = level;
	}

	if (level1 == level2) {
		if (r2->fbc_wm_enabled && !r1->fbc_wm_enabled)
			return r2;
		else
			return r1;
	} else if (level1 > level2) {
		return r1;
	} else {
		return r2;
	}
}

/* dirty bits used to track which watermarks need changes */
#define WM_DIRTY_PIPE(pipe) (1 << (pipe))
#define WM_DIRTY_LINETIME(pipe) (1 << (8 + (pipe)))
#define WM_DIRTY_LP(wm_lp) (1 << (15 + (wm_lp)))
#define WM_DIRTY_LP_ALL (WM_DIRTY_LP(1) | WM_DIRTY_LP(2) | WM_DIRTY_LP(3))
#define WM_DIRTY_FBC (1 << 24)
#define WM_DIRTY_DDB (1 << 25)

static unsigned int ilk_compute_wm_dirty(struct drm_i915_private *dev_priv,
					 const struct ilk_wm_values *old,
					 const struct ilk_wm_values *new)
{
	unsigned int dirty = 0;
	enum pipe pipe;
	int wm_lp;

	for_each_pipe(dev_priv, pipe) {
		if (old->wm_linetime[pipe] != new->wm_linetime[pipe]) {
			dirty |= WM_DIRTY_LINETIME(pipe);
			/* Must disable LP1+ watermarks too */
			dirty |= WM_DIRTY_LP_ALL;
		}

		if (old->wm_pipe[pipe] != new->wm_pipe[pipe]) {
			dirty |= WM_DIRTY_PIPE(pipe);
			/* Must disable LP1+ watermarks too */
			dirty |= WM_DIRTY_LP_ALL;
		}
	}

	if (old->enable_fbc_wm != new->enable_fbc_wm) {
		dirty |= WM_DIRTY_FBC;
		/* Must disable LP1+ watermarks too */
		dirty |= WM_DIRTY_LP_ALL;
	}

	if (old->partitioning != new->partitioning) {
		dirty |= WM_DIRTY_DDB;
		/* Must disable LP1+ watermarks too */
		dirty |= WM_DIRTY_LP_ALL;
	}

	/* LP1+ watermarks already deemed dirty, no need to continue */
	if (dirty & WM_DIRTY_LP_ALL)
		return dirty;

	/* Find the lowest numbered LP1+ watermark in need of an update... */
	for (wm_lp = 1; wm_lp <= 3; wm_lp++) {
		if (old->wm_lp[wm_lp - 1] != new->wm_lp[wm_lp - 1] ||
		    old->wm_lp_spr[wm_lp - 1] != new->wm_lp_spr[wm_lp - 1])
			break;
	}

	/* ...and mark it and all higher numbered LP1+ watermarks as dirty */
	for (; wm_lp <= 3; wm_lp++)
		dirty |= WM_DIRTY_LP(wm_lp);

	return dirty;
}

static bool _ilk_disable_lp_wm(struct drm_i915_private *dev_priv,
			       unsigned int dirty)
{
	struct ilk_wm_values *previous = &dev_priv->wm.hw;
	bool changed = false;

	if (dirty & WM_DIRTY_LP(3) && previous->wm_lp[2] & WM1_LP_SR_EN) {
		previous->wm_lp[2] &= ~WM1_LP_SR_EN;
		I915_WRITE(WM3_LP_ILK, previous->wm_lp[2]);
		changed = true;
	}
	if (dirty & WM_DIRTY_LP(2) && previous->wm_lp[1] & WM1_LP_SR_EN) {
		previous->wm_lp[1] &= ~WM1_LP_SR_EN;
		I915_WRITE(WM2_LP_ILK, previous->wm_lp[1]);
		changed = true;
	}
	if (dirty & WM_DIRTY_LP(1) && previous->wm_lp[0] & WM1_LP_SR_EN) {
		previous->wm_lp[0] &= ~WM1_LP_SR_EN;
		I915_WRITE(WM1_LP_ILK, previous->wm_lp[0]);
		changed = true;
	}

	/*
	 * Don't touch WM1S_LP_EN here.
	 * Doing so could cause underruns.
	 */

	return changed;
}

/*
 * The spec says we shouldn't write when we don't need, because every write
 * causes WMs to be re-evaluated, expending some power.
 */
static void ilk_write_wm_values(struct drm_i915_private *dev_priv,
				struct ilk_wm_values *results)
{
	struct ilk_wm_values *previous = &dev_priv->wm.hw;
	unsigned int dirty;
	uint32_t val;

	dirty = ilk_compute_wm_dirty(dev_priv, previous, results);
	if (!dirty)
		return;

	_ilk_disable_lp_wm(dev_priv, dirty);

	if (dirty & WM_DIRTY_PIPE(PIPE_A))
		I915_WRITE(WM0_PIPEA_ILK, results->wm_pipe[0]);
	if (dirty & WM_DIRTY_PIPE(PIPE_B))
		I915_WRITE(WM0_PIPEB_ILK, results->wm_pipe[1]);
	if (dirty & WM_DIRTY_PIPE(PIPE_C))
		I915_WRITE(WM0_PIPEC_IVB, results->wm_pipe[2]);

	if (dirty & WM_DIRTY_LINETIME(PIPE_A))
		I915_WRITE(PIPE_WM_LINETIME(PIPE_A), results->wm_linetime[0]);
	if (dirty & WM_DIRTY_LINETIME(PIPE_B))
		I915_WRITE(PIPE_WM_LINETIME(PIPE_B), results->wm_linetime[1]);
	if (dirty & WM_DIRTY_LINETIME(PIPE_C))
		I915_WRITE(PIPE_WM_LINETIME(PIPE_C), results->wm_linetime[2]);

	if (dirty & WM_DIRTY_DDB) {
		if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
			val = I915_READ(WM_MISC);
			if (results->partitioning == INTEL_DDB_PART_1_2)
				val &= ~WM_MISC_DATA_PARTITION_5_6;
			else
				val |= WM_MISC_DATA_PARTITION_5_6;
			I915_WRITE(WM_MISC, val);
		} else {
			val = I915_READ(DISP_ARB_CTL2);
			if (results->partitioning == INTEL_DDB_PART_1_2)
				val &= ~DISP_DATA_PARTITION_5_6;
			else
				val |= DISP_DATA_PARTITION_5_6;
			I915_WRITE(DISP_ARB_CTL2, val);
		}
	}

	if (dirty & WM_DIRTY_FBC) {
		val = I915_READ(DISP_ARB_CTL);
		if (results->enable_fbc_wm)
			val &= ~DISP_FBC_WM_DIS;
		else
			val |= DISP_FBC_WM_DIS;
		I915_WRITE(DISP_ARB_CTL, val);
	}

	if (dirty & WM_DIRTY_LP(1) &&
	    previous->wm_lp_spr[0] != results->wm_lp_spr[0])
		I915_WRITE(WM1S_LP_ILK, results->wm_lp_spr[0]);

	if (INTEL_GEN(dev_priv) >= 7) {
		if (dirty & WM_DIRTY_LP(2) && previous->wm_lp_spr[1] != results->wm_lp_spr[1])
			I915_WRITE(WM2S_LP_IVB, results->wm_lp_spr[1]);
		if (dirty & WM_DIRTY_LP(3) && previous->wm_lp_spr[2] != results->wm_lp_spr[2])
			I915_WRITE(WM3S_LP_IVB, results->wm_lp_spr[2]);
	}

	if (dirty & WM_DIRTY_LP(1) && previous->wm_lp[0] != results->wm_lp[0])
		I915_WRITE(WM1_LP_ILK, results->wm_lp[0]);
	if (dirty & WM_DIRTY_LP(2) && previous->wm_lp[1] != results->wm_lp[1])
		I915_WRITE(WM2_LP_ILK, results->wm_lp[1]);
	if (dirty & WM_DIRTY_LP(3) && previous->wm_lp[2] != results->wm_lp[2])
		I915_WRITE(WM3_LP_ILK, results->wm_lp[2]);

	dev_priv->wm.hw = *results;
}

bool ilk_disable_lp_wm(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);

	return _ilk_disable_lp_wm(dev_priv, WM_DIRTY_LP_ALL);
}

/*
 * FIXME: We still don't have the proper code detect if we need to apply the WA,
 * so assume we'll always need it in order to avoid underruns.
 */
static bool skl_needs_memory_bw_wa(struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);

	if (IS_GEN9_BC(dev_priv) || IS_BROXTON(dev_priv))
		return true;

	return false;
}

static bool
intel_has_sagv(struct drm_i915_private *dev_priv)
{
	if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv) ||
	    IS_CANNONLAKE(dev_priv))
		return true;

	if (IS_SKYLAKE(dev_priv) &&
	    dev_priv->sagv_status != I915_SAGV_NOT_CONTROLLED)
		return true;

	return false;
}

/*
 * SAGV dynamically adjusts the system agent voltage and clock frequencies
 * depending on power and performance requirements. The display engine access
 * to system memory is blocked during the adjustment time. Because of the
 * blocking time, having this enabled can cause full system hangs and/or pipe
 * underruns if we don't meet all of the following requirements:
 *
 *  - <= 1 pipe enabled
 *  - All planes can enable watermarks for latencies >= SAGV engine block time
 *  - We're not using an interlaced display configuration
 */
int
intel_enable_sagv(struct drm_i915_private *dev_priv)
{
	int ret;

	if (!intel_has_sagv(dev_priv))
		return 0;

	if (dev_priv->sagv_status == I915_SAGV_ENABLED)
		return 0;

	DRM_DEBUG_KMS("Enabling the SAGV\n");
	mutex_lock(&dev_priv->rps.hw_lock);

	ret = sandybridge_pcode_write(dev_priv, GEN9_PCODE_SAGV_CONTROL,
				      GEN9_SAGV_ENABLE);

	/* We don't need to wait for the SAGV when enabling */
	mutex_unlock(&dev_priv->rps.hw_lock);

	/*
	 * Some skl systems, pre-release machines in particular,
	 * don't actually have an SAGV.
	 */
	if (IS_SKYLAKE(dev_priv) && ret == -ENXIO) {
		DRM_DEBUG_DRIVER("No SAGV found on system, ignoring\n");
		dev_priv->sagv_status = I915_SAGV_NOT_CONTROLLED;
		return 0;
	} else if (ret < 0) {
		DRM_ERROR("Failed to enable the SAGV\n");
		return ret;
	}

	dev_priv->sagv_status = I915_SAGV_ENABLED;
	return 0;
}

int
intel_disable_sagv(struct drm_i915_private *dev_priv)
{
	int ret;

	if (!intel_has_sagv(dev_priv))
		return 0;

	if (dev_priv->sagv_status == I915_SAGV_DISABLED)
		return 0;

	DRM_DEBUG_KMS("Disabling the SAGV\n");
	mutex_lock(&dev_priv->rps.hw_lock);

	/* bspec says to keep retrying for at least 1 ms */
	ret = skl_pcode_request(dev_priv, GEN9_PCODE_SAGV_CONTROL,
				GEN9_SAGV_DISABLE,
				GEN9_SAGV_IS_DISABLED, GEN9_SAGV_IS_DISABLED,
				1);
	mutex_unlock(&dev_priv->rps.hw_lock);

	/*
	 * Some skl systems, pre-release machines in particular,
	 * don't actually have an SAGV.
	 */
	if (IS_SKYLAKE(dev_priv) && ret == -ENXIO) {
		DRM_DEBUG_DRIVER("No SAGV found on system, ignoring\n");
		dev_priv->sagv_status = I915_SAGV_NOT_CONTROLLED;
		return 0;
	} else if (ret < 0) {
		DRM_ERROR("Failed to disable the SAGV (%d)\n", ret);
		return ret;
	}

	dev_priv->sagv_status = I915_SAGV_DISABLED;
	return 0;
}

bool intel_can_enable_sagv(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct intel_crtc *crtc;
	struct intel_plane *plane;
	struct intel_crtc_state *cstate;
	enum pipe pipe;
	int level, latency;
	int sagv_block_time_us = IS_GEN9(dev_priv) ? 30 : 20;

	if (!intel_has_sagv(dev_priv))
		return false;

	/*
	 * SKL+ workaround: bspec recommends we disable the SAGV when we have
	 * more then one pipe enabled
	 *
	 * If there are no active CRTCs, no additional checks need be performed
	 */
	if (hweight32(intel_state->active_crtcs) == 0)
		return true;
	else if (hweight32(intel_state->active_crtcs) > 1)
		return false;

	/* Since we're now guaranteed to only have one active CRTC... */
	pipe = ffs(intel_state->active_crtcs) - 1;
	crtc = intel_get_crtc_for_pipe(dev_priv, pipe);
	cstate = to_intel_crtc_state(crtc->base.state);

	if (crtc->base.state->adjusted_mode.flags & DRM_MODE_FLAG_INTERLACE)
		return false;

	for_each_intel_plane_on_crtc(dev, crtc, plane) {
		struct skl_plane_wm *wm =
			&cstate->wm.skl.optimal.planes[plane->id];

		/* Skip this plane if it's not enabled */
		if (!wm->wm[0].plane_en)
			continue;

		/* Find the highest enabled wm level for this plane */
		for (level = ilk_wm_max_level(dev_priv);
		     !wm->wm[level].plane_en; --level)
		     { }

		latency = dev_priv->wm.skl_latency[level];

		if (skl_needs_memory_bw_wa(intel_state) &&
		    plane->base.state->fb->modifier ==
		    I915_FORMAT_MOD_X_TILED)
			latency += 15;

		/*
		 * If any of the planes on this pipe don't enable wm levels that
		 * incur memory latencies higher than sagv_block_time_us we
		 * can't enable the SAGV.
		 */
		if (latency < sagv_block_time_us)
			return false;
	}

	return true;
}

static void
skl_ddb_get_pipe_allocation_limits(struct drm_device *dev,
				   const struct intel_crtc_state *cstate,
				   struct skl_ddb_entry *alloc, /* out */
				   int *num_active /* out */)
{
	struct drm_atomic_state *state = cstate->base.state;
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_crtc *for_crtc = cstate->base.crtc;
	unsigned int pipe_size, ddb_size;
	int nth_active_pipe;

	if (WARN_ON(!state) || !cstate->base.active) {
		alloc->start = 0;
		alloc->end = 0;
		*num_active = hweight32(dev_priv->active_crtcs);
		return;
	}

	if (intel_state->active_pipe_changes)
		*num_active = hweight32(intel_state->active_crtcs);
	else
		*num_active = hweight32(dev_priv->active_crtcs);

	ddb_size = INTEL_INFO(dev_priv)->ddb_size;
	WARN_ON(ddb_size == 0);

	ddb_size -= 4; /* 4 blocks for bypass path allocation */

	/*
	 * If the state doesn't change the active CRTC's, then there's
	 * no need to recalculate; the existing pipe allocation limits
	 * should remain unchanged.  Note that we're safe from racing
	 * commits since any racing commit that changes the active CRTC
	 * list would need to grab _all_ crtc locks, including the one
	 * we currently hold.
	 */
	if (!intel_state->active_pipe_changes) {
		/*
		 * alloc may be cleared by clear_intel_crtc_state,
		 * copy from old state to be sure
		 */
		*alloc = to_intel_crtc_state(for_crtc->state)->wm.skl.ddb;
		return;
	}

	nth_active_pipe = hweight32(intel_state->active_crtcs &
				    (drm_crtc_mask(for_crtc) - 1));
	pipe_size = ddb_size / hweight32(intel_state->active_crtcs);
	alloc->start = nth_active_pipe * ddb_size / *num_active;
	alloc->end = alloc->start + pipe_size;
}

static unsigned int skl_cursor_allocation(int num_active)
{
	if (num_active == 1)
		return 32;

	return 8;
}

static void skl_ddb_entry_init_from_hw(struct skl_ddb_entry *entry, u32 reg)
{
	entry->start = reg & 0x3ff;
	entry->end = (reg >> 16) & 0x3ff;
	if (entry->end)
		entry->end += 1;
}

void skl_ddb_get_hw_state(struct drm_i915_private *dev_priv,
			  struct skl_ddb_allocation *ddb /* out */)
{
	struct intel_crtc *crtc;

	memset(ddb, 0, sizeof(*ddb));

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		enum intel_display_power_domain power_domain;
		enum plane_id plane_id;
		enum pipe pipe = crtc->pipe;

		power_domain = POWER_DOMAIN_PIPE(pipe);
		if (!intel_display_power_get_if_enabled(dev_priv, power_domain))
			continue;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			u32 val;

			if (plane_id != PLANE_CURSOR)
				val = I915_READ(PLANE_BUF_CFG(pipe, plane_id));
			else
				val = I915_READ(CUR_BUF_CFG(pipe));

			skl_ddb_entry_init_from_hw(&ddb->plane[pipe][plane_id], val);
		}

		intel_display_power_put(dev_priv, power_domain);
	}
}

/*
 * Determines the downscale amount of a plane for the purposes of watermark calculations.
 * The bspec defines downscale amount as:
 *
 * """
 * Horizontal down scale amount = maximum[1, Horizontal source size /
 *                                           Horizontal destination size]
 * Vertical down scale amount = maximum[1, Vertical source size /
 *                                         Vertical destination size]
 * Total down scale amount = Horizontal down scale amount *
 *                           Vertical down scale amount
 * """
 *
 * Return value is provided in 16.16 fixed point form to retain fractional part.
 * Caller should take care of dividing & rounding off the value.
 */
static uint_fixed_16_16_t
skl_plane_downscale_amount(const struct intel_crtc_state *cstate,
			   const struct intel_plane_state *pstate)
{
	struct intel_plane *plane = to_intel_plane(pstate->base.plane);
	uint32_t src_w, src_h, dst_w, dst_h;
	uint_fixed_16_16_t fp_w_ratio, fp_h_ratio;
	uint_fixed_16_16_t downscale_h, downscale_w;

	if (WARN_ON(!intel_wm_plane_visible(cstate, pstate)))
		return u32_to_fixed16(0);

	/* n.b., src is 16.16 fixed point, dst is whole integer */
	if (plane->id == PLANE_CURSOR) {
		/*
		 * Cursors only support 0/180 degree rotation,
		 * hence no need to account for rotation here.
		 */
		src_w = pstate->base.src_w >> 16;
		src_h = pstate->base.src_h >> 16;
		dst_w = pstate->base.crtc_w;
		dst_h = pstate->base.crtc_h;
	} else {
		/*
		 * Src coordinates are already rotated by 270 degrees for
		 * the 90/270 degree plane rotation cases (to match the
		 * GTT mapping), hence no need to account for rotation here.
		 */
		src_w = drm_rect_width(&pstate->base.src) >> 16;
		src_h = drm_rect_height(&pstate->base.src) >> 16;
		dst_w = drm_rect_width(&pstate->base.dst);
		dst_h = drm_rect_height(&pstate->base.dst);
	}

	fp_w_ratio = div_fixed16(src_w, dst_w);
	fp_h_ratio = div_fixed16(src_h, dst_h);
	downscale_w = max_fixed16(fp_w_ratio, u32_to_fixed16(1));
	downscale_h = max_fixed16(fp_h_ratio, u32_to_fixed16(1));

	return mul_fixed16(downscale_w, downscale_h);
}

static uint_fixed_16_16_t
skl_pipe_downscale_amount(const struct intel_crtc_state *crtc_state)
{
	uint_fixed_16_16_t pipe_downscale = u32_to_fixed16(1);

	if (!crtc_state->base.enable)
		return pipe_downscale;

	if (crtc_state->pch_pfit.enabled) {
		uint32_t src_w, src_h, dst_w, dst_h;
		uint32_t pfit_size = crtc_state->pch_pfit.size;
		uint_fixed_16_16_t fp_w_ratio, fp_h_ratio;
		uint_fixed_16_16_t downscale_h, downscale_w;

		src_w = crtc_state->pipe_src_w;
		src_h = crtc_state->pipe_src_h;
		dst_w = pfit_size >> 16;
		dst_h = pfit_size & 0xffff;

		if (!dst_w || !dst_h)
			return pipe_downscale;

		fp_w_ratio = div_fixed16(src_w, dst_w);
		fp_h_ratio = div_fixed16(src_h, dst_h);
		downscale_w = max_fixed16(fp_w_ratio, u32_to_fixed16(1));
		downscale_h = max_fixed16(fp_h_ratio, u32_to_fixed16(1));

		pipe_downscale = mul_fixed16(downscale_w, downscale_h);
	}

	return pipe_downscale;
}

int skl_check_pipe_max_pixel_rate(struct intel_crtc *intel_crtc,
				  struct intel_crtc_state *cstate)
{
	struct drm_crtc_state *crtc_state = &cstate->base;
	struct drm_atomic_state *state = crtc_state->state;
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;
	struct intel_plane_state *intel_pstate;
	int crtc_clock, dotclk;
	uint32_t pipe_max_pixel_rate;
	uint_fixed_16_16_t pipe_downscale;
	uint_fixed_16_16_t max_downscale = u32_to_fixed16(1);

	if (!cstate->base.enable)
		return 0;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		uint_fixed_16_16_t plane_downscale;
		uint_fixed_16_16_t fp_9_div_8 = div_fixed16(9, 8);
		int bpp;

		if (!intel_wm_plane_visible(cstate,
					    to_intel_plane_state(pstate)))
			continue;

		if (WARN_ON(!pstate->fb))
			return -EINVAL;

		intel_pstate = to_intel_plane_state(pstate);
		plane_downscale = skl_plane_downscale_amount(cstate,
							     intel_pstate);
		bpp = pstate->fb->format->cpp[0] * 8;
		if (bpp == 64)
			plane_downscale = mul_fixed16(plane_downscale,
						      fp_9_div_8);

		max_downscale = max_fixed16(plane_downscale, max_downscale);
	}
	pipe_downscale = skl_pipe_downscale_amount(cstate);

	pipe_downscale = mul_fixed16(pipe_downscale, max_downscale);

	crtc_clock = crtc_state->adjusted_mode.crtc_clock;
	dotclk = to_intel_atomic_state(state)->cdclk.logical.cdclk;

	if (IS_GEMINILAKE(to_i915(intel_crtc->base.dev)))
		dotclk *= 2;

	pipe_max_pixel_rate = div_round_up_u32_fixed16(dotclk, pipe_downscale);

	if (pipe_max_pixel_rate < crtc_clock) {
		DRM_DEBUG_KMS("Max supported pixel clock with scaling exceeded\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned int
skl_plane_relative_data_rate(const struct intel_crtc_state *cstate,
			     const struct drm_plane_state *pstate,
			     int y)
{
	struct intel_plane *plane = to_intel_plane(pstate->plane);
	struct intel_plane_state *intel_pstate = to_intel_plane_state(pstate);
	uint32_t data_rate;
	uint32_t width = 0, height = 0;
	struct drm_framebuffer *fb;
	u32 format;
	uint_fixed_16_16_t down_scale_amount;

	if (!intel_pstate->base.visible)
		return 0;

	fb = pstate->fb;
	format = fb->format->format;

	if (plane->id == PLANE_CURSOR)
		return 0;
	if (y && format != DRM_FORMAT_NV12)
		return 0;

	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	width = drm_rect_width(&intel_pstate->base.src) >> 16;
	height = drm_rect_height(&intel_pstate->base.src) >> 16;

	/* for planar format */
	if (format == DRM_FORMAT_NV12) {
		if (y)  /* y-plane data rate */
			data_rate = width * height *
				fb->format->cpp[0];
		else    /* uv-plane data rate */
			data_rate = (width / 2) * (height / 2) *
				fb->format->cpp[1];
	} else {
		/* for packed formats */
		data_rate = width * height * fb->format->cpp[0];
	}

	down_scale_amount = skl_plane_downscale_amount(cstate, intel_pstate);

	return mul_round_up_u32_fixed16(data_rate, down_scale_amount);
}

/*
 * We don't overflow 32 bits. Worst case is 3 planes enabled, each fetching
 * a 8192x4096@32bpp framebuffer:
 *   3 * 4096 * 8192  * 4 < 2^32
 */
static unsigned int
skl_get_total_relative_data_rate(struct intel_crtc_state *intel_cstate,
				 unsigned *plane_data_rate,
				 unsigned *plane_y_data_rate)
{
	struct drm_crtc_state *cstate = &intel_cstate->base;
	struct drm_atomic_state *state = cstate->state;
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;
	unsigned int total_data_rate = 0;

	if (WARN_ON(!state))
		return 0;

	/* Calculate and cache data rate for each plane */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, cstate) {
		enum plane_id plane_id = to_intel_plane(plane)->id;
		unsigned int rate;

		/* packed/uv */
		rate = skl_plane_relative_data_rate(intel_cstate,
						    pstate, 0);
		plane_data_rate[plane_id] = rate;

		total_data_rate += rate;

		/* y-plane */
		rate = skl_plane_relative_data_rate(intel_cstate,
						    pstate, 1);
		plane_y_data_rate[plane_id] = rate;

		total_data_rate += rate;
	}

	return total_data_rate;
}

static uint16_t
skl_ddb_min_alloc(const struct drm_plane_state *pstate,
		  const int y)
{
	struct drm_framebuffer *fb = pstate->fb;
	struct intel_plane_state *intel_pstate = to_intel_plane_state(pstate);
	uint32_t src_w, src_h;
	uint32_t min_scanlines = 8;
	uint8_t plane_bpp;

	if (WARN_ON(!fb))
		return 0;

	/* For packed formats, no y-plane, return 0 */
	if (y && fb->format->format != DRM_FORMAT_NV12)
		return 0;

	/* For Non Y-tile return 8-blocks */
	if (fb->modifier != I915_FORMAT_MOD_Y_TILED &&
	    fb->modifier != I915_FORMAT_MOD_Yf_TILED &&
	    fb->modifier != I915_FORMAT_MOD_Y_TILED_CCS &&
	    fb->modifier != I915_FORMAT_MOD_Yf_TILED_CCS)
		return 8;

	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	src_w = drm_rect_width(&intel_pstate->base.src) >> 16;
	src_h = drm_rect_height(&intel_pstate->base.src) >> 16;

	/* Halve UV plane width and height for NV12 */
	if (fb->format->format == DRM_FORMAT_NV12 && !y) {
		src_w /= 2;
		src_h /= 2;
	}

	if (fb->format->format == DRM_FORMAT_NV12 && !y)
		plane_bpp = fb->format->cpp[1];
	else
		plane_bpp = fb->format->cpp[0];

	if (drm_rotation_90_or_270(pstate->rotation)) {
		switch (plane_bpp) {
		case 1:
			min_scanlines = 32;
			break;
		case 2:
			min_scanlines = 16;
			break;
		case 4:
			min_scanlines = 8;
			break;
		case 8:
			min_scanlines = 4;
			break;
		default:
			WARN(1, "Unsupported pixel depth %u for rotation",
			     plane_bpp);
			min_scanlines = 32;
		}
	}

	return DIV_ROUND_UP((4 * src_w * plane_bpp), 512) * min_scanlines/4 + 3;
}

static void
skl_ddb_calc_min(const struct intel_crtc_state *cstate, int num_active,
		 uint16_t *minimum, uint16_t *y_minimum)
{
	const struct drm_plane_state *pstate;
	struct drm_plane *plane;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, &cstate->base) {
		enum plane_id plane_id = to_intel_plane(plane)->id;

		if (plane_id == PLANE_CURSOR)
			continue;

		if (!pstate->visible)
			continue;

		minimum[plane_id] = skl_ddb_min_alloc(pstate, 0);
		y_minimum[plane_id] = skl_ddb_min_alloc(pstate, 1);
	}

	minimum[PLANE_CURSOR] = skl_cursor_allocation(num_active);
}

static int
skl_allocate_pipe_ddb(struct intel_crtc_state *cstate,
		      struct skl_ddb_allocation *ddb /* out */)
{
	struct drm_atomic_state *state = cstate->base.state;
	struct drm_crtc *crtc = cstate->base.crtc;
	struct drm_device *dev = crtc->dev;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	struct skl_ddb_entry *alloc = &cstate->wm.skl.ddb;
	uint16_t alloc_size, start;
	uint16_t minimum[I915_MAX_PLANES] = {};
	uint16_t y_minimum[I915_MAX_PLANES] = {};
	unsigned int total_data_rate;
	enum plane_id plane_id;
	int num_active;
	unsigned plane_data_rate[I915_MAX_PLANES] = {};
	unsigned plane_y_data_rate[I915_MAX_PLANES] = {};
	uint16_t total_min_blocks = 0;

	/* Clear the partitioning for disabled planes. */
	memset(ddb->plane[pipe], 0, sizeof(ddb->plane[pipe]));
	memset(ddb->y_plane[pipe], 0, sizeof(ddb->y_plane[pipe]));

	if (WARN_ON(!state))
		return 0;

	if (!cstate->base.active) {
		alloc->start = alloc->end = 0;
		return 0;
	}

	skl_ddb_get_pipe_allocation_limits(dev, cstate, alloc, &num_active);
	alloc_size = skl_ddb_entry_size(alloc);
	if (alloc_size == 0)
		return 0;

	skl_ddb_calc_min(cstate, num_active, minimum, y_minimum);

	/*
	 * 1. Allocate the mininum required blocks for each active plane
	 * and allocate the cursor, it doesn't require extra allocation
	 * proportional to the data rate.
	 */

	for_each_plane_id_on_crtc(intel_crtc, plane_id) {
		total_min_blocks += minimum[plane_id];
		total_min_blocks += y_minimum[plane_id];
	}

	if (total_min_blocks > alloc_size) {
		DRM_DEBUG_KMS("Requested display configuration exceeds system DDB limitations");
		DRM_DEBUG_KMS("minimum required %d/%d\n", total_min_blocks,
							alloc_size);
		return -EINVAL;
	}

	alloc_size -= total_min_blocks;
	ddb->plane[pipe][PLANE_CURSOR].start = alloc->end - minimum[PLANE_CURSOR];
	ddb->plane[pipe][PLANE_CURSOR].end = alloc->end;

	/*
	 * 2. Distribute the remaining space in proportion to the amount of
	 * data each plane needs to fetch from memory.
	 *
	 * FIXME: we may not allocate every single block here.
	 */
	total_data_rate = skl_get_total_relative_data_rate(cstate,
							   plane_data_rate,
							   plane_y_data_rate);
	if (total_data_rate == 0)
		return 0;

	start = alloc->start;
	for_each_plane_id_on_crtc(intel_crtc, plane_id) {
		unsigned int data_rate, y_data_rate;
		uint16_t plane_blocks, y_plane_blocks = 0;

		if (plane_id == PLANE_CURSOR)
			continue;

		data_rate = plane_data_rate[plane_id];

		/*
		 * allocation for (packed formats) or (uv-plane part of planar format):
		 * promote the expression to 64 bits to avoid overflowing, the
		 * result is < available as data_rate / total_data_rate < 1
		 */
		plane_blocks = minimum[plane_id];
		plane_blocks += div_u64((uint64_t)alloc_size * data_rate,
					total_data_rate);

		/* Leave disabled planes at (0,0) */
		if (data_rate) {
			ddb->plane[pipe][plane_id].start = start;
			ddb->plane[pipe][plane_id].end = start + plane_blocks;
		}

		start += plane_blocks;

		/*
		 * allocation for y_plane part of planar format:
		 */
		y_data_rate = plane_y_data_rate[plane_id];

		y_plane_blocks = y_minimum[plane_id];
		y_plane_blocks += div_u64((uint64_t)alloc_size * y_data_rate,
					total_data_rate);

		if (y_data_rate) {
			ddb->y_plane[pipe][plane_id].start = start;
			ddb->y_plane[pipe][plane_id].end = start + y_plane_blocks;
		}

		start += y_plane_blocks;
	}

	return 0;
}

/*
 * The max latency should be 257 (max the punit can code is 255 and we add 2us
 * for the read latency) and cpp should always be <= 8, so that
 * should allow pixel_rate up to ~2 GHz which seems sufficient since max
 * 2xcdclk is 1350 MHz and the pixel rate should never exceed that.
*/
static uint_fixed_16_16_t
skl_wm_method1(const struct drm_i915_private *dev_priv, uint32_t pixel_rate,
	       uint8_t cpp, uint32_t latency)
{
	uint32_t wm_intermediate_val;
	uint_fixed_16_16_t ret;

	if (latency == 0)
		return FP_16_16_MAX;

	wm_intermediate_val = latency * pixel_rate * cpp;
	ret = div_fixed16(wm_intermediate_val, 1000 * 512);

	if (INTEL_GEN(dev_priv) >= 10)
		ret = add_fixed16_u32(ret, 1);

	return ret;
}

static uint_fixed_16_16_t skl_wm_method2(uint32_t pixel_rate,
			uint32_t pipe_htotal,
			uint32_t latency,
			uint_fixed_16_16_t plane_blocks_per_line)
{
	uint32_t wm_intermediate_val;
	uint_fixed_16_16_t ret;

	if (latency == 0)
		return FP_16_16_MAX;

	wm_intermediate_val = latency * pixel_rate;
	wm_intermediate_val = DIV_ROUND_UP(wm_intermediate_val,
					   pipe_htotal * 1000);
	ret = mul_u32_fixed16(wm_intermediate_val, plane_blocks_per_line);
	return ret;
}

static uint_fixed_16_16_t
intel_get_linetime_us(struct intel_crtc_state *cstate)
{
	uint32_t pixel_rate;
	uint32_t crtc_htotal;
	uint_fixed_16_16_t linetime_us;

	if (!cstate->base.active)
		return u32_to_fixed16(0);

	pixel_rate = cstate->pixel_rate;

	if (WARN_ON(pixel_rate == 0))
		return u32_to_fixed16(0);

	crtc_htotal = cstate->base.adjusted_mode.crtc_htotal;
	linetime_us = div_fixed16(crtc_htotal * 1000, pixel_rate);

	return linetime_us;
}

static uint32_t
skl_adjusted_plane_pixel_rate(const struct intel_crtc_state *cstate,
			      const struct intel_plane_state *pstate)
{
	uint64_t adjusted_pixel_rate;
	uint_fixed_16_16_t downscale_amount;

	/* Shouldn't reach here on disabled planes... */
	if (WARN_ON(!intel_wm_plane_visible(cstate, pstate)))
		return 0;

	/*
	 * Adjusted plane pixel rate is just the pipe's adjusted pixel rate
	 * with additional adjustments for plane-specific scaling.
	 */
	adjusted_pixel_rate = cstate->pixel_rate;
	downscale_amount = skl_plane_downscale_amount(cstate, pstate);

	return mul_round_up_u32_fixed16(adjusted_pixel_rate,
					    downscale_amount);
}

static int skl_compute_plane_wm(const struct drm_i915_private *dev_priv,
				struct intel_crtc_state *cstate,
				const struct intel_plane_state *intel_pstate,
				uint16_t ddb_allocation,
				int level,
				uint16_t *out_blocks, /* out */
				uint8_t *out_lines, /* out */
				bool *enabled /* out */)
{
	struct intel_plane *plane = to_intel_plane(intel_pstate->base.plane);
	const struct drm_plane_state *pstate = &intel_pstate->base;
	const struct drm_framebuffer *fb = pstate->fb;
	uint32_t latency = dev_priv->wm.skl_latency[level];
	uint_fixed_16_16_t method1, method2;
	uint_fixed_16_16_t plane_blocks_per_line;
	uint_fixed_16_16_t selected_result;
	uint32_t interm_pbpl;
	uint32_t plane_bytes_per_line;
	uint32_t res_blocks, res_lines;
	uint8_t cpp;
	uint32_t width = 0;
	uint32_t plane_pixel_rate;
	uint_fixed_16_16_t y_tile_minimum;
	uint32_t y_min_scanlines;
	struct intel_atomic_state *state =
		to_intel_atomic_state(cstate->base.state);
	bool apply_memory_bw_wa = skl_needs_memory_bw_wa(state);
	bool y_tiled, x_tiled;

	if (latency == 0 ||
	    !intel_wm_plane_visible(cstate, intel_pstate)) {
		*enabled = false;
		return 0;
	}

	y_tiled = fb->modifier == I915_FORMAT_MOD_Y_TILED ||
		  fb->modifier == I915_FORMAT_MOD_Yf_TILED ||
		  fb->modifier == I915_FORMAT_MOD_Y_TILED_CCS ||
		  fb->modifier == I915_FORMAT_MOD_Yf_TILED_CCS;
	x_tiled = fb->modifier == I915_FORMAT_MOD_X_TILED;

	/* Display WA #1141: kbl,cfl */
	if ((IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv)) &&
	    dev_priv->ipc_enabled)
		latency += 4;

	if (apply_memory_bw_wa && x_tiled)
		latency += 15;

	if (plane->id == PLANE_CURSOR) {
		width = intel_pstate->base.crtc_w;
	} else {
		/*
		 * Src coordinates are already rotated by 270 degrees for
		 * the 90/270 degree plane rotation cases (to match the
		 * GTT mapping), hence no need to account for rotation here.
		 */
		width = drm_rect_width(&intel_pstate->base.src) >> 16;
	}

	cpp = (fb->format->format == DRM_FORMAT_NV12) ? fb->format->cpp[1] :
							fb->format->cpp[0];
	plane_pixel_rate = skl_adjusted_plane_pixel_rate(cstate, intel_pstate);

	if (drm_rotation_90_or_270(pstate->rotation)) {

		switch (cpp) {
		case 1:
			y_min_scanlines = 16;
			break;
		case 2:
			y_min_scanlines = 8;
			break;
		case 4:
			y_min_scanlines = 4;
			break;
		default:
			MISSING_CASE(cpp);
			return -EINVAL;
		}
	} else {
		y_min_scanlines = 4;
	}

	if (apply_memory_bw_wa)
		y_min_scanlines *= 2;

	plane_bytes_per_line = width * cpp;
	if (y_tiled) {
		interm_pbpl = DIV_ROUND_UP(plane_bytes_per_line *
					   y_min_scanlines, 512);

		if (INTEL_GEN(dev_priv) >= 10)
			interm_pbpl++;

		plane_blocks_per_line = div_fixed16(interm_pbpl,
							y_min_scanlines);
	} else if (x_tiled && INTEL_GEN(dev_priv) == 9) {
		interm_pbpl = DIV_ROUND_UP(plane_bytes_per_line, 512);
		plane_blocks_per_line = u32_to_fixed16(interm_pbpl);
	} else {
		interm_pbpl = DIV_ROUND_UP(plane_bytes_per_line, 512) + 1;
		plane_blocks_per_line = u32_to_fixed16(interm_pbpl);
	}

	method1 = skl_wm_method1(dev_priv, plane_pixel_rate, cpp, latency);
	method2 = skl_wm_method2(plane_pixel_rate,
				 cstate->base.adjusted_mode.crtc_htotal,
				 latency,
				 plane_blocks_per_line);

	y_tile_minimum = mul_u32_fixed16(y_min_scanlines,
					 plane_blocks_per_line);

	if (y_tiled) {
		selected_result = max_fixed16(method2, y_tile_minimum);
	} else {
		uint32_t linetime_us;

		linetime_us = fixed16_to_u32_round_up(
				intel_get_linetime_us(cstate));
		if ((cpp * cstate->base.adjusted_mode.crtc_htotal / 512 < 1) &&
		    (plane_bytes_per_line / 512 < 1))
			selected_result = method2;
		else if (ddb_allocation >=
			 fixed16_to_u32_round_up(plane_blocks_per_line))
			selected_result = min_fixed16(method1, method2);
		else if (latency >= linetime_us)
			selected_result = min_fixed16(method1, method2);
		else
			selected_result = method1;
	}

	res_blocks = fixed16_to_u32_round_up(selected_result) + 1;
	res_lines = div_round_up_fixed16(selected_result,
					 plane_blocks_per_line);

	/* Display WA #1125: skl,bxt,kbl,glk */
	if (level == 0 &&
	    (fb->modifier == I915_FORMAT_MOD_Y_TILED_CCS ||
	     fb->modifier == I915_FORMAT_MOD_Yf_TILED_CCS))
		res_blocks += fixed16_to_u32_round_up(y_tile_minimum);

	/* Display WA #1126: skl,bxt,kbl,glk */
	if (level >= 1 && level <= 7) {
		if (y_tiled) {
			res_blocks += fixed16_to_u32_round_up(y_tile_minimum);
			res_lines += y_min_scanlines;
		} else {
			res_blocks++;
		}
	}

	if (res_blocks >= ddb_allocation || res_lines > 31) {
		*enabled = false;

		/*
		 * If there are no valid level 0 watermarks, then we can't
		 * support this display configuration.
		 */
		if (level) {
			return 0;
		} else {
			struct drm_plane *plane = pstate->plane;

			DRM_DEBUG_KMS("Requested display configuration exceeds system watermark limitations\n");
			DRM_DEBUG_KMS("[PLANE:%d:%s] blocks required = %u/%u, lines required = %u/31\n",
				      plane->base.id, plane->name,
				      res_blocks, ddb_allocation, res_lines);
			return -EINVAL;
		}
	}

	*out_blocks = res_blocks;
	*out_lines = res_lines;
	*enabled = true;

	return 0;
}

static int
skl_compute_wm_levels(const struct drm_i915_private *dev_priv,
		      struct skl_ddb_allocation *ddb,
		      struct intel_crtc_state *cstate,
		      const struct intel_plane_state *intel_pstate,
		      struct skl_plane_wm *wm)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(cstate->base.crtc);
	struct drm_plane *plane = intel_pstate->base.plane;
	struct intel_plane *intel_plane = to_intel_plane(plane);
	uint16_t ddb_blocks;
	enum pipe pipe = intel_crtc->pipe;
	int level, max_level = ilk_wm_max_level(dev_priv);
	int ret;

	if (WARN_ON(!intel_pstate->base.fb))
		return -EINVAL;

	ddb_blocks = skl_ddb_entry_size(&ddb->plane[pipe][intel_plane->id]);

	for (level = 0; level <= max_level; level++) {
		struct skl_wm_level *result = &wm->wm[level];

		ret = skl_compute_plane_wm(dev_priv,
					   cstate,
					   intel_pstate,
					   ddb_blocks,
					   level,
					   &result->plane_res_b,
					   &result->plane_res_l,
					   &result->plane_en);
		if (ret)
			return ret;
	}

	return 0;
}

static uint32_t
skl_compute_linetime_wm(struct intel_crtc_state *cstate)
{
	struct drm_atomic_state *state = cstate->base.state;
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	uint_fixed_16_16_t linetime_us;
	uint32_t linetime_wm;

	linetime_us = intel_get_linetime_us(cstate);

	if (is_fixed16_zero(linetime_us))
		return 0;

	linetime_wm = fixed16_to_u32_round_up(mul_u32_fixed16(8, linetime_us));

	/* Display WA #1135: bxt. */
	if (IS_BROXTON(dev_priv) && dev_priv->ipc_enabled)
		linetime_wm = DIV_ROUND_UP(linetime_wm, 2);

	return linetime_wm;
}

static void skl_compute_transition_wm(struct intel_crtc_state *cstate,
				      struct skl_wm_level *trans_wm /* out */)
{
	if (!cstate->base.active)
		return;

	/* Until we know more, just disable transition WMs */
	trans_wm->plane_en = false;
}

static int skl_build_pipe_wm(struct intel_crtc_state *cstate,
			     struct skl_ddb_allocation *ddb,
			     struct skl_pipe_wm *pipe_wm)
{
	struct drm_device *dev = cstate->base.crtc->dev;
	struct drm_crtc_state *crtc_state = &cstate->base;
	const struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;
	struct skl_plane_wm *wm;
	int ret;

	/*
	 * We'll only calculate watermarks for planes that are actually
	 * enabled, so make sure all other planes are set as disabled.
	 */
	memset(pipe_wm->planes, 0, sizeof(pipe_wm->planes));

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		const struct intel_plane_state *intel_pstate =
						to_intel_plane_state(pstate);
		enum plane_id plane_id = to_intel_plane(plane)->id;

		wm = &pipe_wm->planes[plane_id];

		ret = skl_compute_wm_levels(dev_priv, ddb, cstate,
					    intel_pstate, wm);
		if (ret)
			return ret;
		skl_compute_transition_wm(cstate, &wm->trans_wm);
	}
	pipe_wm->linetime = skl_compute_linetime_wm(cstate);

	return 0;
}

static void skl_ddb_entry_write(struct drm_i915_private *dev_priv,
				i915_reg_t reg,
				const struct skl_ddb_entry *entry)
{
	if (entry->end)
		I915_WRITE(reg, (entry->end - 1) << 16 | entry->start);
	else
		I915_WRITE(reg, 0);
}

static void skl_write_wm_level(struct drm_i915_private *dev_priv,
			       i915_reg_t reg,
			       const struct skl_wm_level *level)
{
	uint32_t val = 0;

	if (level->plane_en) {
		val |= PLANE_WM_EN;
		val |= level->plane_res_b;
		val |= level->plane_res_l << PLANE_WM_LINES_SHIFT;
	}

	I915_WRITE(reg, val);
}

static void skl_write_plane_wm(struct intel_crtc *intel_crtc,
			       const struct skl_plane_wm *wm,
			       const struct skl_ddb_allocation *ddb,
			       enum plane_id plane_id)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int level, max_level = ilk_wm_max_level(dev_priv);
	enum pipe pipe = intel_crtc->pipe;

	for (level = 0; level <= max_level; level++) {
		skl_write_wm_level(dev_priv, PLANE_WM(pipe, plane_id, level),
				   &wm->wm[level]);
	}
	skl_write_wm_level(dev_priv, PLANE_WM_TRANS(pipe, plane_id),
			   &wm->trans_wm);

	skl_ddb_entry_write(dev_priv, PLANE_BUF_CFG(pipe, plane_id),
			    &ddb->plane[pipe][plane_id]);
	skl_ddb_entry_write(dev_priv, PLANE_NV12_BUF_CFG(pipe, plane_id),
			    &ddb->y_plane[pipe][plane_id]);
}

static void skl_write_cursor_wm(struct intel_crtc *intel_crtc,
				const struct skl_plane_wm *wm,
				const struct skl_ddb_allocation *ddb)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	int level, max_level = ilk_wm_max_level(dev_priv);
	enum pipe pipe = intel_crtc->pipe;

	for (level = 0; level <= max_level; level++) {
		skl_write_wm_level(dev_priv, CUR_WM(pipe, level),
				   &wm->wm[level]);
	}
	skl_write_wm_level(dev_priv, CUR_WM_TRANS(pipe), &wm->trans_wm);

	skl_ddb_entry_write(dev_priv, CUR_BUF_CFG(pipe),
			    &ddb->plane[pipe][PLANE_CURSOR]);
}

bool skl_wm_level_equals(const struct skl_wm_level *l1,
			 const struct skl_wm_level *l2)
{
	if (l1->plane_en != l2->plane_en)
		return false;

	/* If both planes aren't enabled, the rest shouldn't matter */
	if (!l1->plane_en)
		return true;

	return (l1->plane_res_l == l2->plane_res_l &&
		l1->plane_res_b == l2->plane_res_b);
}

static inline bool skl_ddb_entries_overlap(const struct skl_ddb_entry *a,
					   const struct skl_ddb_entry *b)
{
	return a->start < b->end && b->start < a->end;
}

bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry **entries,
				 const struct skl_ddb_entry *ddb,
				 int ignore)
{
	int i;

	for (i = 0; i < I915_MAX_PIPES; i++)
		if (i != ignore && entries[i] &&
		    skl_ddb_entries_overlap(ddb, entries[i]))
			return true;

	return false;
}

static int skl_update_pipe_wm(struct drm_crtc_state *cstate,
			      const struct skl_pipe_wm *old_pipe_wm,
			      struct skl_pipe_wm *pipe_wm, /* out */
			      struct skl_ddb_allocation *ddb, /* out */
			      bool *changed /* out */)
{
	struct intel_crtc_state *intel_cstate = to_intel_crtc_state(cstate);
	int ret;

	ret = skl_build_pipe_wm(intel_cstate, ddb, pipe_wm);
	if (ret)
		return ret;

	if (!memcmp(old_pipe_wm, pipe_wm, sizeof(*pipe_wm)))
		*changed = false;
	else
		*changed = true;

	return 0;
}

static uint32_t
pipes_modified(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *cstate;
	uint32_t i, ret = 0;

	for_each_new_crtc_in_state(state, crtc, cstate, i)
		ret |= drm_crtc_mask(crtc);

	return ret;
}

static int
skl_ddb_add_affected_planes(struct intel_crtc_state *cstate)
{
	struct drm_atomic_state *state = cstate->base.state;
	struct drm_device *dev = state->dev;
	struct drm_crtc *crtc = cstate->base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct skl_ddb_allocation *new_ddb = &intel_state->wm_results.ddb;
	struct skl_ddb_allocation *cur_ddb = &dev_priv->wm.skl_hw.ddb;
	struct drm_plane_state *plane_state;
	struct drm_plane *plane;
	enum pipe pipe = intel_crtc->pipe;

	WARN_ON(!drm_atomic_get_existing_crtc_state(state, crtc));

	drm_for_each_plane_mask(plane, dev, cstate->base.plane_mask) {
		enum plane_id plane_id = to_intel_plane(plane)->id;

		if (skl_ddb_entry_equal(&cur_ddb->plane[pipe][plane_id],
					&new_ddb->plane[pipe][plane_id]) &&
		    skl_ddb_entry_equal(&cur_ddb->y_plane[pipe][plane_id],
					&new_ddb->y_plane[pipe][plane_id]))
			continue;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);
	}

	return 0;
}

static int
skl_compute_ddb(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct intel_crtc *intel_crtc;
	struct skl_ddb_allocation *ddb = &intel_state->wm_results.ddb;
	uint32_t realloc_pipes = pipes_modified(state);
	int ret;

	/*
	 * If this is our first atomic update following hardware readout,
	 * we can't trust the DDB that the BIOS programmed for us.  Let's
	 * pretend that all pipes switched active status so that we'll
	 * ensure a full DDB recompute.
	 */
	if (dev_priv->wm.distrust_bios_wm) {
		ret = drm_modeset_lock(&dev->mode_config.connection_mutex,
				       state->acquire_ctx);
		if (ret)
			return ret;

		intel_state->active_pipe_changes = ~0;

		/*
		 * We usually only initialize intel_state->active_crtcs if we
		 * we're doing a modeset; make sure this field is always
		 * initialized during the sanitization process that happens
		 * on the first commit too.
		 */
		if (!intel_state->modeset)
			intel_state->active_crtcs = dev_priv->active_crtcs;
	}

	/*
	 * If the modeset changes which CRTC's are active, we need to
	 * recompute the DDB allocation for *all* active pipes, even
	 * those that weren't otherwise being modified in any way by this
	 * atomic commit.  Due to the shrinking of the per-pipe allocations
	 * when new active CRTC's are added, it's possible for a pipe that
	 * we were already using and aren't changing at all here to suddenly
	 * become invalid if its DDB needs exceeds its new allocation.
	 *
	 * Note that if we wind up doing a full DDB recompute, we can't let
	 * any other display updates race with this transaction, so we need
	 * to grab the lock on *all* CRTC's.
	 */
	if (intel_state->active_pipe_changes) {
		realloc_pipes = ~0;
		intel_state->wm_results.dirty_pipes = ~0;
	}

	/*
	 * We're not recomputing for the pipes not included in the commit, so
	 * make sure we start with the current state.
	 */
	memcpy(ddb, &dev_priv->wm.skl_hw.ddb, sizeof(*ddb));

	for_each_intel_crtc_mask(dev, intel_crtc, realloc_pipes) {
		struct intel_crtc_state *cstate;

		cstate = intel_atomic_get_crtc_state(state, intel_crtc);
		if (IS_ERR(cstate))
			return PTR_ERR(cstate);

		ret = skl_allocate_pipe_ddb(cstate, ddb);
		if (ret)
			return ret;

		ret = skl_ddb_add_affected_planes(cstate);
		if (ret)
			return ret;
	}

	return 0;
}

static void
skl_copy_wm_for_pipe(struct skl_wm_values *dst,
		     struct skl_wm_values *src,
		     enum pipe pipe)
{
	memcpy(dst->ddb.y_plane[pipe], src->ddb.y_plane[pipe],
	       sizeof(dst->ddb.y_plane[pipe]));
	memcpy(dst->ddb.plane[pipe], src->ddb.plane[pipe],
	       sizeof(dst->ddb.plane[pipe]));
}

static void
skl_print_wm_changes(const struct drm_atomic_state *state)
{
	const struct drm_device *dev = state->dev;
	const struct drm_i915_private *dev_priv = to_i915(dev);
	const struct intel_atomic_state *intel_state =
		to_intel_atomic_state(state);
	const struct drm_crtc *crtc;
	const struct drm_crtc_state *cstate;
	const struct intel_plane *intel_plane;
	const struct skl_ddb_allocation *old_ddb = &dev_priv->wm.skl_hw.ddb;
	const struct skl_ddb_allocation *new_ddb = &intel_state->wm_results.ddb;
	int i;

	for_each_new_crtc_in_state(state, crtc, cstate, i) {
		const struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
		enum pipe pipe = intel_crtc->pipe;

		for_each_intel_plane_on_crtc(dev, intel_crtc, intel_plane) {
			enum plane_id plane_id = intel_plane->id;
			const struct skl_ddb_entry *old, *new;

			old = &old_ddb->plane[pipe][plane_id];
			new = &new_ddb->plane[pipe][plane_id];

			if (skl_ddb_entry_equal(old, new))
				continue;

			DRM_DEBUG_ATOMIC("[PLANE:%d:%s] ddb (%d - %d) -> (%d - %d)\n",
					 intel_plane->base.base.id,
					 intel_plane->base.name,
					 old->start, old->end,
					 new->start, new->end);
		}
	}
}

static int
skl_compute_wm(struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *cstate;
	struct intel_atomic_state *intel_state = to_intel_atomic_state(state);
	struct skl_wm_values *results = &intel_state->wm_results;
	struct drm_device *dev = state->dev;
	struct skl_pipe_wm *pipe_wm;
	bool changed = false;
	int ret, i;

	/*
	 * When we distrust bios wm we always need to recompute to set the
	 * expected DDB allocations for each CRTC.
	 */
	if (to_i915(dev)->wm.distrust_bios_wm)
		changed = true;

	/*
	 * If this transaction isn't actually touching any CRTC's, don't
	 * bother with watermark calculation.  Note that if we pass this
	 * test, we're guaranteed to hold at least one CRTC state mutex,
	 * which means we can safely use values like dev_priv->active_crtcs
	 * since any racing commits that want to update them would need to
	 * hold _all_ CRTC state mutexes.
	 */
	for_each_new_crtc_in_state(state, crtc, cstate, i)
		changed = true;

	if (!changed)
		return 0;

	/* Clear all dirty flags */
	results->dirty_pipes = 0;

	ret = skl_compute_ddb(state);
	if (ret)
		return ret;

	/*
	 * Calculate WM's for all pipes that are part of this transaction.
	 * Note that the DDB allocation above may have added more CRTC's that
	 * weren't otherwise being modified (and set bits in dirty_pipes) if
	 * pipe allocations had to change.
	 *
	 * FIXME:  Now that we're doing this in the atomic check phase, we
	 * should allow skl_update_pipe_wm() to return failure in cases where
	 * no suitable watermark values can be found.
	 */
	for_each_new_crtc_in_state(state, crtc, cstate, i) {
		struct intel_crtc_state *intel_cstate =
			to_intel_crtc_state(cstate);
		const struct skl_pipe_wm *old_pipe_wm =
			&to_intel_crtc_state(crtc->state)->wm.skl.optimal;

		pipe_wm = &intel_cstate->wm.skl.optimal;
		ret = skl_update_pipe_wm(cstate, old_pipe_wm, pipe_wm,
					 &results->ddb, &changed);
		if (ret)
			return ret;

		if (changed)
			results->dirty_pipes |= drm_crtc_mask(crtc);

		if ((results->dirty_pipes & drm_crtc_mask(crtc)) == 0)
			/* This pipe's WM's did not change */
			continue;

		intel_cstate->update_wm_pre = true;
	}

	skl_print_wm_changes(state);

	return 0;
}

static void skl_atomic_update_crtc_wm(struct intel_atomic_state *state,
				      struct intel_crtc_state *cstate)
{
	struct intel_crtc *crtc = to_intel_crtc(cstate->base.crtc);
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct skl_pipe_wm *pipe_wm = &cstate->wm.skl.optimal;
	const struct skl_ddb_allocation *ddb = &state->wm_results.ddb;
	enum pipe pipe = crtc->pipe;
	enum plane_id plane_id;

	if (!(state->wm_results.dirty_pipes & drm_crtc_mask(&crtc->base)))
		return;

	I915_WRITE(PIPE_WM_LINETIME(pipe), pipe_wm->linetime);

	for_each_plane_id_on_crtc(crtc, plane_id) {
		if (plane_id != PLANE_CURSOR)
			skl_write_plane_wm(crtc, &pipe_wm->planes[plane_id],
					   ddb, plane_id);
		else
			skl_write_cursor_wm(crtc, &pipe_wm->planes[plane_id],
					    ddb);
	}
}

static void skl_initial_wm(struct intel_atomic_state *state,
			   struct intel_crtc_state *cstate)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(cstate->base.crtc);
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct skl_wm_values *results = &state->wm_results;
	struct skl_wm_values *hw_vals = &dev_priv->wm.skl_hw;
	enum pipe pipe = intel_crtc->pipe;

	if ((results->dirty_pipes & drm_crtc_mask(&intel_crtc->base)) == 0)
		return;

	mutex_lock(&dev_priv->wm.wm_mutex);

	if (cstate->base.active_changed)
		skl_atomic_update_crtc_wm(state, cstate);

	skl_copy_wm_for_pipe(hw_vals, results, pipe);

	mutex_unlock(&dev_priv->wm.wm_mutex);
}

static void ilk_compute_wm_config(struct drm_device *dev,
				  struct intel_wm_config *config)
{
	struct intel_crtc *crtc;

	/* Compute the currently _active_ config */
	for_each_intel_crtc(dev, crtc) {
		const struct intel_pipe_wm *wm = &crtc->wm.active.ilk;

		if (!wm->pipe_enabled)
			continue;

		config->sprites_enabled |= wm->sprites_enabled;
		config->sprites_scaled |= wm->sprites_scaled;
		config->num_pipes_active++;
	}
}

static void ilk_program_watermarks(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_pipe_wm lp_wm_1_2 = {}, lp_wm_5_6 = {}, *best_lp_wm;
	struct ilk_wm_maximums max;
	struct intel_wm_config config = {};
	struct ilk_wm_values results = {};
	enum intel_ddb_partitioning partitioning;

	ilk_compute_wm_config(dev, &config);

	ilk_compute_wm_maximums(dev, 1, &config, INTEL_DDB_PART_1_2, &max);
	ilk_wm_merge(dev, &config, &max, &lp_wm_1_2);

	/* 5/6 split only in single pipe config on IVB+ */
	if (INTEL_GEN(dev_priv) >= 7 &&
	    config.num_pipes_active == 1 && config.sprites_enabled) {
		ilk_compute_wm_maximums(dev, 1, &config, INTEL_DDB_PART_5_6, &max);
		ilk_wm_merge(dev, &config, &max, &lp_wm_5_6);

		best_lp_wm = ilk_find_best_result(dev, &lp_wm_1_2, &lp_wm_5_6);
	} else {
		best_lp_wm = &lp_wm_1_2;
	}

	partitioning = (best_lp_wm == &lp_wm_1_2) ?
		       INTEL_DDB_PART_1_2 : INTEL_DDB_PART_5_6;

	ilk_compute_wm_results(dev, best_lp_wm, partitioning, &results);

	ilk_write_wm_values(dev_priv, &results);
}

static void ilk_initial_watermarks(struct intel_atomic_state *state,
				   struct intel_crtc_state *cstate)
{
	struct drm_i915_private *dev_priv = to_i915(cstate->base.crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(cstate->base.crtc);

	mutex_lock(&dev_priv->wm.wm_mutex);
	intel_crtc->wm.active.ilk = cstate->wm.ilk.intermediate;
	ilk_program_watermarks(dev_priv);
	mutex_unlock(&dev_priv->wm.wm_mutex);
}

static void ilk_optimize_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc_state *cstate)
{
	struct drm_i915_private *dev_priv = to_i915(cstate->base.crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(cstate->base.crtc);

	mutex_lock(&dev_priv->wm.wm_mutex);
	if (cstate->wm.need_postvbl_update) {
		intel_crtc->wm.active.ilk = cstate->wm.ilk.optimal;
		ilk_program_watermarks(dev_priv);
	}
	mutex_unlock(&dev_priv->wm.wm_mutex);
}

static inline void skl_wm_level_from_reg_val(uint32_t val,
					     struct skl_wm_level *level)
{
	level->plane_en = val & PLANE_WM_EN;
	level->plane_res_b = val & PLANE_WM_BLOCKS_MASK;
	level->plane_res_l = (val >> PLANE_WM_LINES_SHIFT) &
		PLANE_WM_LINES_MASK;
}

void skl_pipe_wm_get_hw_state(struct drm_crtc *crtc,
			      struct skl_pipe_wm *out)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	enum pipe pipe = intel_crtc->pipe;
	int level, max_level;
	enum plane_id plane_id;
	uint32_t val;

	max_level = ilk_wm_max_level(dev_priv);

	for_each_plane_id_on_crtc(intel_crtc, plane_id) {
		struct skl_plane_wm *wm = &out->planes[plane_id];

		for (level = 0; level <= max_level; level++) {
			if (plane_id != PLANE_CURSOR)
				val = I915_READ(PLANE_WM(pipe, plane_id, level));
			else
				val = I915_READ(CUR_WM(pipe, level));

			skl_wm_level_from_reg_val(val, &wm->wm[level]);
		}

		if (plane_id != PLANE_CURSOR)
			val = I915_READ(PLANE_WM_TRANS(pipe, plane_id));
		else
			val = I915_READ(CUR_WM_TRANS(pipe));

		skl_wm_level_from_reg_val(val, &wm->trans_wm);
	}

	if (!intel_crtc->active)
		return;

	out->linetime = I915_READ(PIPE_WM_LINETIME(pipe));
}

void skl_wm_get_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct skl_wm_values *hw = &dev_priv->wm.skl_hw;
	struct skl_ddb_allocation *ddb = &dev_priv->wm.skl_hw.ddb;
	struct drm_crtc *crtc;
	struct intel_crtc *intel_crtc;
	struct intel_crtc_state *cstate;

	skl_ddb_get_hw_state(dev_priv, ddb);
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		intel_crtc = to_intel_crtc(crtc);
		cstate = to_intel_crtc_state(crtc->state);

		skl_pipe_wm_get_hw_state(crtc, &cstate->wm.skl.optimal);

		if (intel_crtc->active)
			hw->dirty_pipes |= drm_crtc_mask(crtc);
	}

	if (dev_priv->active_crtcs) {
		/* Fully recompute DDB on first atomic commit */
		dev_priv->wm.distrust_bios_wm = true;
	} else {
		/* Easy/common case; just sanitize DDB now if everything off */
		memset(ddb, 0, sizeof(*ddb));
	}
}

static void ilk_pipe_wm_get_hw_state(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct ilk_wm_values *hw = &dev_priv->wm.hw;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct intel_crtc_state *cstate = to_intel_crtc_state(crtc->state);
	struct intel_pipe_wm *active = &cstate->wm.ilk.optimal;
	enum pipe pipe = intel_crtc->pipe;
	static const i915_reg_t wm0_pipe_reg[] = {
		[PIPE_A] = WM0_PIPEA_ILK,
		[PIPE_B] = WM0_PIPEB_ILK,
		[PIPE_C] = WM0_PIPEC_IVB,
	};

	hw->wm_pipe[pipe] = I915_READ(wm0_pipe_reg[pipe]);
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		hw->wm_linetime[pipe] = I915_READ(PIPE_WM_LINETIME(pipe));

	memset(active, 0, sizeof(*active));

	active->pipe_enabled = intel_crtc->active;

	if (active->pipe_enabled) {
		u32 tmp = hw->wm_pipe[pipe];

		/*
		 * For active pipes LP0 watermark is marked as
		 * enabled, and LP1+ watermaks as disabled since
		 * we can't really reverse compute them in case
		 * multiple pipes are active.
		 */
		active->wm[0].enable = true;
		active->wm[0].pri_val = (tmp & WM0_PIPE_PLANE_MASK) >> WM0_PIPE_PLANE_SHIFT;
		active->wm[0].spr_val = (tmp & WM0_PIPE_SPRITE_MASK) >> WM0_PIPE_SPRITE_SHIFT;
		active->wm[0].cur_val = tmp & WM0_PIPE_CURSOR_MASK;
		active->linetime = hw->wm_linetime[pipe];
	} else {
		int level, max_level = ilk_wm_max_level(dev_priv);

		/*
		 * For inactive pipes, all watermark levels
		 * should be marked as enabled but zeroed,
		 * which is what we'd compute them to.
		 */
		for (level = 0; level <= max_level; level++)
			active->wm[level].enable = true;
	}

	intel_crtc->wm.active.ilk = *active;
}

#define _FW_WM(value, plane) \
	(((value) & DSPFW_ ## plane ## _MASK) >> DSPFW_ ## plane ## _SHIFT)
#define _FW_WM_VLV(value, plane) \
	(((value) & DSPFW_ ## plane ## _MASK_VLV) >> DSPFW_ ## plane ## _SHIFT)

static void g4x_read_wm_values(struct drm_i915_private *dev_priv,
			       struct g4x_wm_values *wm)
{
	uint32_t tmp;

	tmp = I915_READ(DSPFW1);
	wm->sr.plane = _FW_WM(tmp, SR);
	wm->pipe[PIPE_B].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORB);
	wm->pipe[PIPE_B].plane[PLANE_PRIMARY] = _FW_WM(tmp, PLANEB);
	wm->pipe[PIPE_A].plane[PLANE_PRIMARY] = _FW_WM(tmp, PLANEA);

	tmp = I915_READ(DSPFW2);
	wm->fbc_en = tmp & DSPFW_FBC_SR_EN;
	wm->sr.fbc = _FW_WM(tmp, FBC_SR);
	wm->hpll.fbc = _FW_WM(tmp, FBC_HPLL_SR);
	wm->pipe[PIPE_B].plane[PLANE_SPRITE0] = _FW_WM(tmp, SPRITEB);
	wm->pipe[PIPE_A].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORA);
	wm->pipe[PIPE_A].plane[PLANE_SPRITE0] = _FW_WM(tmp, SPRITEA);

	tmp = I915_READ(DSPFW3);
	wm->hpll_en = tmp & DSPFW_HPLL_SR_EN;
	wm->sr.cursor = _FW_WM(tmp, CURSOR_SR);
	wm->hpll.cursor = _FW_WM(tmp, HPLL_CURSOR);
	wm->hpll.plane = _FW_WM(tmp, HPLL_SR);
}

static void vlv_read_wm_values(struct drm_i915_private *dev_priv,
			       struct vlv_wm_values *wm)
{
	enum pipe pipe;
	uint32_t tmp;

	for_each_pipe(dev_priv, pipe) {
		tmp = I915_READ(VLV_DDL(pipe));

		wm->ddl[pipe].plane[PLANE_PRIMARY] =
			(tmp >> DDL_PLANE_SHIFT) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
		wm->ddl[pipe].plane[PLANE_CURSOR] =
			(tmp >> DDL_CURSOR_SHIFT) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
		wm->ddl[pipe].plane[PLANE_SPRITE0] =
			(tmp >> DDL_SPRITE_SHIFT(0)) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
		wm->ddl[pipe].plane[PLANE_SPRITE1] =
			(tmp >> DDL_SPRITE_SHIFT(1)) & (DDL_PRECISION_HIGH | DRAIN_LATENCY_MASK);
	}

	tmp = I915_READ(DSPFW1);
	wm->sr.plane = _FW_WM(tmp, SR);
	wm->pipe[PIPE_B].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORB);
	wm->pipe[PIPE_B].plane[PLANE_PRIMARY] = _FW_WM_VLV(tmp, PLANEB);
	wm->pipe[PIPE_A].plane[PLANE_PRIMARY] = _FW_WM_VLV(tmp, PLANEA);

	tmp = I915_READ(DSPFW2);
	wm->pipe[PIPE_A].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITEB);
	wm->pipe[PIPE_A].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORA);
	wm->pipe[PIPE_A].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEA);

	tmp = I915_READ(DSPFW3);
	wm->sr.cursor = _FW_WM(tmp, CURSOR_SR);

	if (IS_CHERRYVIEW(dev_priv)) {
		tmp = I915_READ(DSPFW7_CHV);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITED);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEC);

		tmp = I915_READ(DSPFW8_CHV);
		wm->pipe[PIPE_C].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITEF);
		wm->pipe[PIPE_C].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEE);

		tmp = I915_READ(DSPFW9_CHV);
		wm->pipe[PIPE_C].plane[PLANE_PRIMARY] = _FW_WM_VLV(tmp, PLANEC);
		wm->pipe[PIPE_C].plane[PLANE_CURSOR] = _FW_WM(tmp, CURSORC);

		tmp = I915_READ(DSPHOWM);
		wm->sr.plane |= _FW_WM(tmp, SR_HI) << 9;
		wm->pipe[PIPE_C].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITEF_HI) << 8;
		wm->pipe[PIPE_C].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEE_HI) << 8;
		wm->pipe[PIPE_C].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEC_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITED_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEC_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEA_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEA_HI) << 8;
	} else {
		tmp = I915_READ(DSPFW7);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] = _FW_WM_VLV(tmp, SPRITED);
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] = _FW_WM_VLV(tmp, SPRITEC);

		tmp = I915_READ(DSPHOWM);
		wm->sr.plane |= _FW_WM(tmp, SR_HI) << 9;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITED_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEC_HI) << 8;
		wm->pipe[PIPE_B].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE1] |= _FW_WM(tmp, SPRITEB_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_SPRITE0] |= _FW_WM(tmp, SPRITEA_HI) << 8;
		wm->pipe[PIPE_A].plane[PLANE_PRIMARY] |= _FW_WM(tmp, PLANEA_HI) << 8;
	}
}

#undef _FW_WM
#undef _FW_WM_VLV

void g4x_wm_get_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct g4x_wm_values *wm = &dev_priv->wm.g4x;
	struct intel_crtc *crtc;

	g4x_read_wm_values(dev_priv, wm);

	wm->cxsr = I915_READ(FW_BLC_SELF) & FW_BLC_SELF_EN;

	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct g4x_wm_state *active = &crtc->wm.active.g4x;
		struct g4x_pipe_wm *raw;
		enum pipe pipe = crtc->pipe;
		enum plane_id plane_id;
		int level, max_level;

		active->cxsr = wm->cxsr;
		active->hpll_en = wm->hpll_en;
		active->fbc_en = wm->fbc_en;

		active->sr = wm->sr;
		active->hpll = wm->hpll;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			active->wm.plane[plane_id] =
				wm->pipe[pipe].plane[plane_id];
		}

		if (wm->cxsr && wm->hpll_en)
			max_level = G4X_WM_LEVEL_HPLL;
		else if (wm->cxsr)
			max_level = G4X_WM_LEVEL_SR;
		else
			max_level = G4X_WM_LEVEL_NORMAL;

		level = G4X_WM_LEVEL_NORMAL;
		raw = &crtc_state->wm.g4x.raw[level];
		for_each_plane_id_on_crtc(crtc, plane_id)
			raw->plane[plane_id] = active->wm.plane[plane_id];

		if (++level > max_level)
			goto out;

		raw = &crtc_state->wm.g4x.raw[level];
		raw->plane[PLANE_PRIMARY] = active->sr.plane;
		raw->plane[PLANE_CURSOR] = active->sr.cursor;
		raw->plane[PLANE_SPRITE0] = 0;
		raw->fbc = active->sr.fbc;

		if (++level > max_level)
			goto out;

		raw = &crtc_state->wm.g4x.raw[level];
		raw->plane[PLANE_PRIMARY] = active->hpll.plane;
		raw->plane[PLANE_CURSOR] = active->hpll.cursor;
		raw->plane[PLANE_SPRITE0] = 0;
		raw->fbc = active->hpll.fbc;

	out:
		for_each_plane_id_on_crtc(crtc, plane_id)
			g4x_raw_plane_wm_set(crtc_state, level,
					     plane_id, USHRT_MAX);
		g4x_raw_fbc_wm_set(crtc_state, level, USHRT_MAX);

		crtc_state->wm.g4x.optimal = *active;
		crtc_state->wm.g4x.intermediate = *active;

		DRM_DEBUG_KMS("Initial watermarks: pipe %c, plane=%d, cursor=%d, sprite=%d\n",
			      pipe_name(pipe),
			      wm->pipe[pipe].plane[PLANE_PRIMARY],
			      wm->pipe[pipe].plane[PLANE_CURSOR],
			      wm->pipe[pipe].plane[PLANE_SPRITE0]);
	}

	DRM_DEBUG_KMS("Initial SR watermarks: plane=%d, cursor=%d fbc=%d\n",
		      wm->sr.plane, wm->sr.cursor, wm->sr.fbc);
	DRM_DEBUG_KMS("Initial HPLL watermarks: plane=%d, SR cursor=%d fbc=%d\n",
		      wm->hpll.plane, wm->hpll.cursor, wm->hpll.fbc);
	DRM_DEBUG_KMS("Initial SR=%s HPLL=%s FBC=%s\n",
		      yesno(wm->cxsr), yesno(wm->hpll_en), yesno(wm->fbc_en));
}

void g4x_wm_sanitize(struct drm_i915_private *dev_priv)
{
	struct intel_plane *plane;
	struct intel_crtc *crtc;

	mutex_lock(&dev_priv->wm.wm_mutex);

	for_each_intel_plane(&dev_priv->drm, plane) {
		struct intel_crtc *crtc =
			intel_get_crtc_for_pipe(dev_priv, plane->pipe);
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct g4x_wm_state *wm_state = &crtc_state->wm.g4x.optimal;
		enum plane_id plane_id = plane->id;
		int level;

		if (plane_state->base.visible)
			continue;

		for (level = 0; level < 3; level++) {
			struct g4x_pipe_wm *raw =
				&crtc_state->wm.g4x.raw[level];

			raw->plane[plane_id] = 0;
			wm_state->wm.plane[plane_id] = 0;
		}

		if (plane_id == PLANE_PRIMARY) {
			for (level = 0; level < 3; level++) {
				struct g4x_pipe_wm *raw =
					&crtc_state->wm.g4x.raw[level];
				raw->fbc = 0;
			}

			wm_state->sr.fbc = 0;
			wm_state->hpll.fbc = 0;
			wm_state->fbc_en = false;
		}
	}

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		crtc_state->wm.g4x.intermediate =
			crtc_state->wm.g4x.optimal;
		crtc->wm.active.g4x = crtc_state->wm.g4x.optimal;
	}

	g4x_program_watermarks(dev_priv);

	mutex_unlock(&dev_priv->wm.wm_mutex);
}

void vlv_wm_get_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct vlv_wm_values *wm = &dev_priv->wm.vlv;
	struct intel_crtc *crtc;
	u32 val;

	vlv_read_wm_values(dev_priv, wm);

	wm->cxsr = I915_READ(FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;
	wm->level = VLV_WM_LEVEL_PM2;

	if (IS_CHERRYVIEW(dev_priv)) {
		mutex_lock(&dev_priv->rps.hw_lock);

		val = vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ);
		if (val & DSP_MAXFIFO_PM5_ENABLE)
			wm->level = VLV_WM_LEVEL_PM5;

		/*
		 * If DDR DVFS is disabled in the BIOS, Punit
		 * will never ack the request. So if that happens
		 * assume we don't have to enable/disable DDR DVFS
		 * dynamically. To test that just set the REQ_ACK
		 * bit to poke the Punit, but don't change the
		 * HIGH/LOW bits so that we don't actually change
		 * the current state.
		 */
		val = vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2);
		val |= FORCE_DDR_FREQ_REQ_ACK;
		vlv_punit_write(dev_priv, PUNIT_REG_DDR_SETUP2, val);

		if (wait_for((vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2) &
			      FORCE_DDR_FREQ_REQ_ACK) == 0, 3)) {
			DRM_DEBUG_KMS("Punit not acking DDR DVFS request, "
				      "assuming DDR DVFS is disabled\n");
			dev_priv->wm.max_level = VLV_WM_LEVEL_PM5;
		} else {
			val = vlv_punit_read(dev_priv, PUNIT_REG_DDR_SETUP2);
			if ((val & FORCE_DDR_HIGH_FREQ) == 0)
				wm->level = VLV_WM_LEVEL_DDR_DVFS;
		}

		mutex_unlock(&dev_priv->rps.hw_lock);
	}

	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct vlv_wm_state *active = &crtc->wm.active.vlv;
		const struct vlv_fifo_state *fifo_state =
			&crtc_state->wm.vlv.fifo_state;
		enum pipe pipe = crtc->pipe;
		enum plane_id plane_id;
		int level;

		vlv_get_fifo_size(crtc_state);

		active->num_levels = wm->level + 1;
		active->cxsr = wm->cxsr;

		for (level = 0; level < active->num_levels; level++) {
			struct g4x_pipe_wm *raw =
				&crtc_state->wm.vlv.raw[level];

			active->sr[level].plane = wm->sr.plane;
			active->sr[level].cursor = wm->sr.cursor;

			for_each_plane_id_on_crtc(crtc, plane_id) {
				active->wm[level].plane[plane_id] =
					wm->pipe[pipe].plane[plane_id];

				raw->plane[plane_id] =
					vlv_invert_wm_value(active->wm[level].plane[plane_id],
							    fifo_state->plane[plane_id]);
			}
		}

		for_each_plane_id_on_crtc(crtc, plane_id)
			vlv_raw_plane_wm_set(crtc_state, level,
					     plane_id, USHRT_MAX);
		vlv_invalidate_wms(crtc, active, level);

		crtc_state->wm.vlv.optimal = *active;
		crtc_state->wm.vlv.intermediate = *active;

		DRM_DEBUG_KMS("Initial watermarks: pipe %c, plane=%d, cursor=%d, sprite0=%d, sprite1=%d\n",
			      pipe_name(pipe),
			      wm->pipe[pipe].plane[PLANE_PRIMARY],
			      wm->pipe[pipe].plane[PLANE_CURSOR],
			      wm->pipe[pipe].plane[PLANE_SPRITE0],
			      wm->pipe[pipe].plane[PLANE_SPRITE1]);
	}

	DRM_DEBUG_KMS("Initial watermarks: SR plane=%d, SR cursor=%d level=%d cxsr=%d\n",
		      wm->sr.plane, wm->sr.cursor, wm->level, wm->cxsr);
}

void vlv_wm_sanitize(struct drm_i915_private *dev_priv)
{
	struct intel_plane *plane;
	struct intel_crtc *crtc;

	mutex_lock(&dev_priv->wm.wm_mutex);

	for_each_intel_plane(&dev_priv->drm, plane) {
		struct intel_crtc *crtc =
			intel_get_crtc_for_pipe(dev_priv, plane->pipe);
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);
		struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		struct vlv_wm_state *wm_state = &crtc_state->wm.vlv.optimal;
		const struct vlv_fifo_state *fifo_state =
			&crtc_state->wm.vlv.fifo_state;
		enum plane_id plane_id = plane->id;
		int level;

		if (plane_state->base.visible)
			continue;

		for (level = 0; level < wm_state->num_levels; level++) {
			struct g4x_pipe_wm *raw =
				&crtc_state->wm.vlv.raw[level];

			raw->plane[plane_id] = 0;

			wm_state->wm[level].plane[plane_id] =
				vlv_invert_wm_value(raw->plane[plane_id],
						    fifo_state->plane[plane_id]);
		}
	}

	for_each_intel_crtc(&dev_priv->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		crtc_state->wm.vlv.intermediate =
			crtc_state->wm.vlv.optimal;
		crtc->wm.active.vlv = crtc_state->wm.vlv.optimal;
	}

	vlv_program_watermarks(dev_priv);

	mutex_unlock(&dev_priv->wm.wm_mutex);
}

void ilk_wm_get_hw_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct ilk_wm_values *hw = &dev_priv->wm.hw;
	struct drm_crtc *crtc;

	for_each_crtc(dev, crtc)
		ilk_pipe_wm_get_hw_state(crtc);

	hw->wm_lp[0] = I915_READ(WM1_LP_ILK);
	hw->wm_lp[1] = I915_READ(WM2_LP_ILK);
	hw->wm_lp[2] = I915_READ(WM3_LP_ILK);

	hw->wm_lp_spr[0] = I915_READ(WM1S_LP_ILK);
	if (INTEL_GEN(dev_priv) >= 7) {
		hw->wm_lp_spr[1] = I915_READ(WM2S_LP_IVB);
		hw->wm_lp_spr[2] = I915_READ(WM3S_LP_IVB);
	}

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		hw->partitioning = (I915_READ(WM_MISC) & WM_MISC_DATA_PARTITION_5_6) ?
			INTEL_DDB_PART_5_6 : INTEL_DDB_PART_1_2;
	else if (IS_IVYBRIDGE(dev_priv))
		hw->partitioning = (I915_READ(DISP_ARB_CTL2) & DISP_DATA_PARTITION_5_6) ?
			INTEL_DDB_PART_5_6 : INTEL_DDB_PART_1_2;

	hw->enable_fbc_wm =
		!(I915_READ(DISP_ARB_CTL) & DISP_FBC_WM_DIS);
}

/**
 * intel_update_watermarks - update FIFO watermark values based on current modes
 *
 * Calculate watermark values for the various WM regs based on current mode
 * and plane configuration.
 *
 * There are several cases to deal with here:
 *   - normal (i.e. non-self-refresh)
 *   - self-refresh (SR) mode
 *   - lines are large relative to FIFO size (buffer can hold up to 2)
 *   - lines are small relative to FIFO size (buffer can hold more than 2
 *     lines), so need to account for TLB latency
 *
 *   The normal calculation is:
 *     watermark = dotclock * bytes per pixel * latency
 *   where latency is platform & configuration dependent (we assume pessimal
 *   values here).
 *
 *   The SR calculation is:
 *     watermark = (trunc(latency/line time)+1) * surface width *
 *       bytes per pixel
 *   where
 *     line time = htotal / dotclock
 *     surface width = hdisplay for normal plane and 64 for cursor
 *   and latency is assumed to be high, as above.
 *
 * The final value programmed to the register should always be rounded up,
 * and include an extra 2 entries to account for clock crossings.
 *
 * We don't use the sprite, so we can ignore that.  And on Crestline we have
 * to set the non-SR watermarks to 8.
 */
void intel_update_watermarks(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (dev_priv->display.update_wm)
		dev_priv->display.update_wm(crtc);
}

/*
 * Lock protecting IPS related data structures
 */
DEFINE_SPINLOCK(mchdev_lock);

/* Global for IPS driver to get at the current i915 device. Protected by
 * mchdev_lock. */
static struct drm_i915_private *i915_mch_dev;

bool ironlake_set_drps(struct drm_i915_private *dev_priv, u8 val)
{
	u16 rgvswctl;

	lockdep_assert_held(&mchdev_lock);

	rgvswctl = I915_READ16(MEMSWCTL);
	if (rgvswctl & MEMCTL_CMD_STS) {
		DRM_DEBUG("gpu busy, RCS change rejected\n");
		return false; /* still busy with another command */
	}

	rgvswctl = (MEMCTL_CMD_CHFREQ << MEMCTL_CMD_SHIFT) |
		(val << MEMCTL_FREQ_SHIFT) | MEMCTL_SFCAVM;
	I915_WRITE16(MEMSWCTL, rgvswctl);
	POSTING_READ16(MEMSWCTL);

	rgvswctl |= MEMCTL_CMD_STS;
	I915_WRITE16(MEMSWCTL, rgvswctl);

	return true;
}

static void ironlake_enable_drps(struct drm_i915_private *dev_priv)
{
	u32 rgvmodectl;
	u8 fmax, fmin, fstart, vstart;

	spin_lock_irq(&mchdev_lock);

	rgvmodectl = I915_READ(MEMMODECTL);

	/* Enable temp reporting */
	I915_WRITE16(PMMISC, I915_READ(PMMISC) | MCPPCE_EN);
	I915_WRITE16(TSC1, I915_READ(TSC1) | TSE);

	/* 100ms RC evaluation intervals */
	I915_WRITE(RCUPEI, 100000);
	I915_WRITE(RCDNEI, 100000);

	/* Set max/min thresholds to 90ms and 80ms respectively */
	I915_WRITE(RCBMAXAVG, 90000);
	I915_WRITE(RCBMINAVG, 80000);

	I915_WRITE(MEMIHYST, 1);

	/* Set up min, max, and cur for interrupt handling */
	fmax = (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT;
	fmin = (rgvmodectl & MEMMODE_FMIN_MASK);
	fstart = (rgvmodectl & MEMMODE_FSTART_MASK) >>
		MEMMODE_FSTART_SHIFT;

	vstart = (I915_READ(PXVFREQ(fstart)) & PXVFREQ_PX_MASK) >>
		PXVFREQ_PX_SHIFT;

	dev_priv->ips.fmax = fmax; /* IPS callback will increase this */
	dev_priv->ips.fstart = fstart;

	dev_priv->ips.max_delay = fstart;
	dev_priv->ips.min_delay = fmin;
	dev_priv->ips.cur_delay = fstart;

	DRM_DEBUG_DRIVER("fmax: %d, fmin: %d, fstart: %d\n",
			 fmax, fmin, fstart);

	I915_WRITE(MEMINTREN, MEMINT_CX_SUPR_EN | MEMINT_EVAL_CHG_EN);

	/*
	 * Interrupts will be enabled in ironlake_irq_postinstall
	 */

	I915_WRITE(VIDSTART, vstart);
	POSTING_READ(VIDSTART);

	rgvmodectl |= MEMMODE_SWMODE_EN;
	I915_WRITE(MEMMODECTL, rgvmodectl);

	if (wait_for_atomic((I915_READ(MEMSWCTL) & MEMCTL_CMD_STS) == 0, 10))
		DRM_ERROR("stuck trying to change perf mode\n");
	mdelay(1);

	ironlake_set_drps(dev_priv, fstart);

	dev_priv->ips.last_count1 = I915_READ(DMIEC) +
		I915_READ(DDREC) + I915_READ(CSIEC);
	dev_priv->ips.last_time1 = jiffies_to_msecs(jiffies);
	dev_priv->ips.last_count2 = I915_READ(GFXEC);
	dev_priv->ips.last_time2 = ktime_get_raw_ns();

	spin_unlock_irq(&mchdev_lock);
}

static void ironlake_disable_drps(struct drm_i915_private *dev_priv)
{
	u16 rgvswctl;

	spin_lock_irq(&mchdev_lock);

	rgvswctl = I915_READ16(MEMSWCTL);

	/* Ack interrupts, disable EFC interrupt */
	I915_WRITE(MEMINTREN, I915_READ(MEMINTREN) & ~MEMINT_EVAL_CHG_EN);
	I915_WRITE(MEMINTRSTS, MEMINT_EVAL_CHG);
	I915_WRITE(DEIER, I915_READ(DEIER) & ~DE_PCU_EVENT);
	I915_WRITE(DEIIR, DE_PCU_EVENT);
	I915_WRITE(DEIMR, I915_READ(DEIMR) | DE_PCU_EVENT);

	/* Go back to the starting frequency */
	ironlake_set_drps(dev_priv, dev_priv->ips.fstart);
	mdelay(1);
	rgvswctl |= MEMCTL_CMD_STS;
	I915_WRITE(MEMSWCTL, rgvswctl);
	mdelay(1);

	spin_unlock_irq(&mchdev_lock);
}

/* There's a funny hw issue where the hw returns all 0 when reading from
 * GEN6_RP_INTERRUPT_LIMITS. Hence we always need to compute the desired value
 * ourselves, instead of doing a rmw cycle (which might result in us clearing
 * all limits and the gpu stuck at whatever frequency it is at atm).
 */
static u32 intel_rps_limits(struct drm_i915_private *dev_priv, u8 val)
{
	u32 limits;

	/* Only set the down limit when we've reached the lowest level to avoid
	 * getting more interrupts, otherwise leave this clear. This prevents a
	 * race in the hw when coming out of rc6: There's a tiny window where
	 * the hw runs at the minimal clock before selecting the desired
	 * frequency, if the down threshold expires in that window we will not
	 * receive a down interrupt. */
	if (INTEL_GEN(dev_priv) >= 9) {
		limits = (dev_priv->rps.max_freq_softlimit) << 23;
		if (val <= dev_priv->rps.min_freq_softlimit)
			limits |= (dev_priv->rps.min_freq_softlimit) << 14;
	} else {
		limits = dev_priv->rps.max_freq_softlimit << 24;
		if (val <= dev_priv->rps.min_freq_softlimit)
			limits |= dev_priv->rps.min_freq_softlimit << 16;
	}

	return limits;
}

static void gen6_set_rps_thresholds(struct drm_i915_private *dev_priv, u8 val)
{
	int new_power;
	u32 threshold_up = 0, threshold_down = 0; /* in % */
	u32 ei_up = 0, ei_down = 0;

	new_power = dev_priv->rps.power;
	switch (dev_priv->rps.power) {
	case LOW_POWER:
		if (val > dev_priv->rps.efficient_freq + 1 &&
		    val > dev_priv->rps.cur_freq)
			new_power = BETWEEN;
		break;

	case BETWEEN:
		if (val <= dev_priv->rps.efficient_freq &&
		    val < dev_priv->rps.cur_freq)
			new_power = LOW_POWER;
		else if (val >= dev_priv->rps.rp0_freq &&
			 val > dev_priv->rps.cur_freq)
			new_power = HIGH_POWER;
		break;

	case HIGH_POWER:
		if (val < (dev_priv->rps.rp1_freq + dev_priv->rps.rp0_freq) >> 1 &&
		    val < dev_priv->rps.cur_freq)
			new_power = BETWEEN;
		break;
	}
	/* Max/min bins are special */
	if (val <= dev_priv->rps.min_freq_softlimit)
		new_power = LOW_POWER;
	if (val >= dev_priv->rps.max_freq_softlimit)
		new_power = HIGH_POWER;
	if (new_power == dev_priv->rps.power)
		return;

	/* Note the units here are not exactly 1us, but 1280ns. */
	switch (new_power) {
	case LOW_POWER:
		/* Upclock if more than 95% busy over 16ms */
		ei_up = 16000;
		threshold_up = 95;

		/* Downclock if less than 85% busy over 32ms */
		ei_down = 32000;
		threshold_down = 85;
		break;

	case BETWEEN:
		/* Upclock if more than 90% busy over 13ms */
		ei_up = 13000;
		threshold_up = 90;

		/* Downclock if less than 75% busy over 32ms */
		ei_down = 32000;
		threshold_down = 75;
		break;

	case HIGH_POWER:
		/* Upclock if more than 85% busy over 10ms */
		ei_up = 10000;
		threshold_up = 85;

		/* Downclock if less than 60% busy over 32ms */
		ei_down = 32000;
		threshold_down = 60;
		break;
	}

	/* When byt can survive without system hang with dynamic
	 * sw freq adjustments, this restriction can be lifted.
	 */
	if (IS_VALLEYVIEW(dev_priv))
		goto skip_hw_write;

	I915_WRITE(GEN6_RP_UP_EI,
		   GT_INTERVAL_FROM_US(dev_priv, ei_up));
	I915_WRITE(GEN6_RP_UP_THRESHOLD,
		   GT_INTERVAL_FROM_US(dev_priv,
				       ei_up * threshold_up / 100));

	I915_WRITE(GEN6_RP_DOWN_EI,
		   GT_INTERVAL_FROM_US(dev_priv, ei_down));
	I915_WRITE(GEN6_RP_DOWN_THRESHOLD,
		   GT_INTERVAL_FROM_US(dev_priv,
				       ei_down * threshold_down / 100));

	I915_WRITE(GEN6_RP_CONTROL,
		   GEN6_RP_MEDIA_TURBO |
		   GEN6_RP_MEDIA_HW_NORMAL_MODE |
		   GEN6_RP_MEDIA_IS_GFX |
		   GEN6_RP_ENABLE |
		   GEN6_RP_UP_BUSY_AVG |
		   GEN6_RP_DOWN_IDLE_AVG);

skip_hw_write:
	dev_priv->rps.power = new_power;
	dev_priv->rps.up_threshold = threshold_up;
	dev_priv->rps.down_threshold = threshold_down;
	dev_priv->rps.last_adj = 0;
}

static u32 gen6_rps_pm_mask(struct drm_i915_private *dev_priv, u8 val)
{
	u32 mask = 0;

	/* We use UP_EI_EXPIRED interupts for both up/down in manual mode */
	if (val > dev_priv->rps.min_freq_softlimit)
		mask |= GEN6_PM_RP_UP_EI_EXPIRED | GEN6_PM_RP_DOWN_THRESHOLD | GEN6_PM_RP_DOWN_TIMEOUT;
	if (val < dev_priv->rps.max_freq_softlimit)
		mask |= GEN6_PM_RP_UP_EI_EXPIRED | GEN6_PM_RP_UP_THRESHOLD;

	mask &= dev_priv->pm_rps_events;

	return gen6_sanitize_rps_pm_mask(dev_priv, ~mask);
}

/* gen6_set_rps is called to update the frequency request, but should also be
 * called when the range (min_delay and max_delay) is modified so that we can
 * update the GEN6_RP_INTERRUPT_LIMITS register accordingly. */
static int gen6_set_rps(struct drm_i915_private *dev_priv, u8 val)
{
	/* min/max delay may still have been modified so be sure to
	 * write the limits value.
	 */
	if (val != dev_priv->rps.cur_freq) {
		gen6_set_rps_thresholds(dev_priv, val);

		if (INTEL_GEN(dev_priv) >= 9)
			I915_WRITE(GEN6_RPNSWREQ,
				   GEN9_FREQUENCY(val));
		else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
			I915_WRITE(GEN6_RPNSWREQ,
				   HSW_FREQUENCY(val));
		else
			I915_WRITE(GEN6_RPNSWREQ,
				   GEN6_FREQUENCY(val) |
				   GEN6_OFFSET(0) |
				   GEN6_AGGRESSIVE_TURBO);
	}

	/* Make sure we continue to get interrupts
	 * until we hit the minimum or maximum frequencies.
	 */
	I915_WRITE(GEN6_RP_INTERRUPT_LIMITS, intel_rps_limits(dev_priv, val));
	I915_WRITE(GEN6_PMINTRMSK, gen6_rps_pm_mask(dev_priv, val));

	dev_priv->rps.cur_freq = val;
	trace_intel_gpu_freq_change(intel_gpu_freq(dev_priv, val));

	return 0;
}

static int valleyview_set_rps(struct drm_i915_private *dev_priv, u8 val)
{
	int err;

	if (WARN_ONCE(IS_CHERRYVIEW(dev_priv) && (val & 1),
		      "Odd GPU freq value\n"))
		val &= ~1;

	I915_WRITE(GEN6_PMINTRMSK, gen6_rps_pm_mask(dev_priv, val));

	if (val != dev_priv->rps.cur_freq) {
		err = vlv_punit_write(dev_priv, PUNIT_REG_GPU_FREQ_REQ, val);
		if (err)
			return err;

		gen6_set_rps_thresholds(dev_priv, val);
	}

	dev_priv->rps.cur_freq = val;
	trace_intel_gpu_freq_change(intel_gpu_freq(dev_priv, val));

	return 0;
}

/* vlv_set_rps_idle: Set the frequency to idle, if Gfx clocks are down
 *
 * * If Gfx is Idle, then
 * 1. Forcewake Media well.
 * 2. Request idle freq.
 * 3. Release Forcewake of Media well.
*/
static void vlv_set_rps_idle(struct drm_i915_private *dev_priv)
{
	u32 val = dev_priv->rps.idle_freq;
	int err;

	if (dev_priv->rps.cur_freq <= val)
		return;

	/* The punit delays the write of the frequency and voltage until it
	 * determines the GPU is awake. During normal usage we don't want to
	 * waste power changing the frequency if the GPU is sleeping (rc6).
	 * However, the GPU and driver is now idle and we do not want to delay
	 * switching to minimum voltage (reducing power whilst idle) as we do
	 * not expect to be woken in the near future and so must flush the
	 * change by waking the device.
	 *
	 * We choose to take the media powerwell (either would do to trick the
	 * punit into committing the voltage change) as that takes a lot less
	 * power than the render powerwell.
	 */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_MEDIA);
	err = valleyview_set_rps(dev_priv, val);
	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_MEDIA);

	if (err)
		DRM_ERROR("Failed to set RPS for idle\n");
}

void gen6_rps_busy(struct drm_i915_private *dev_priv)
{
	mutex_lock(&dev_priv->rps.hw_lock);
	if (dev_priv->rps.enabled) {
		u8 freq;

		if (dev_priv->pm_rps_events & GEN6_PM_RP_UP_EI_EXPIRED)
			gen6_rps_reset_ei(dev_priv);
		I915_WRITE(GEN6_PMINTRMSK,
			   gen6_rps_pm_mask(dev_priv, dev_priv->rps.cur_freq));

		gen6_enable_rps_interrupts(dev_priv);

		/* Use the user's desired frequency as a guide, but for better
		 * performance, jump directly to RPe as our starting frequency.
		 */
		freq = max(dev_priv->rps.cur_freq,
			   dev_priv->rps.efficient_freq);

		if (intel_set_rps(dev_priv,
				  clamp(freq,
					dev_priv->rps.min_freq_softlimit,
					dev_priv->rps.max_freq_softlimit)))
			DRM_DEBUG_DRIVER("Failed to set idle frequency\n");
	}
	mutex_unlock(&dev_priv->rps.hw_lock);
}

void gen6_rps_idle(struct drm_i915_private *dev_priv)
{
	/* Flush our bottom-half so that it does not race with us
	 * setting the idle frequency and so that it is bounded by
	 * our rpm wakeref. And then disable the interrupts to stop any
	 * futher RPS reclocking whilst we are asleep.
	 */
	gen6_disable_rps_interrupts(dev_priv);

	mutex_lock(&dev_priv->rps.hw_lock);
	if (dev_priv->rps.enabled) {
		if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			vlv_set_rps_idle(dev_priv);
		else
			gen6_set_rps(dev_priv, dev_priv->rps.idle_freq);
		dev_priv->rps.last_adj = 0;
		I915_WRITE(GEN6_PMINTRMSK,
			   gen6_sanitize_rps_pm_mask(dev_priv, ~0));
	}
	mutex_unlock(&dev_priv->rps.hw_lock);
}

void gen6_rps_boost(struct drm_i915_gem_request *rq,
		    struct intel_rps_client *rps)
{
	struct drm_i915_private *i915 = rq->i915;
	bool boost;

	/* This is intentionally racy! We peek at the state here, then
	 * validate inside the RPS worker.
	 */
	if (!i915->rps.enabled)
		return;

	boost = false;
	spin_lock_irq(&rq->lock);
	if (!rq->waitboost && !i915_gem_request_completed(rq)) {
		atomic_inc(&i915->rps.num_waiters);
		rq->waitboost = true;
		boost = true;
	}
	spin_unlock_irq(&rq->lock);
	if (!boost)
		return;

	if (READ_ONCE(i915->rps.cur_freq) < i915->rps.boost_freq)
		schedule_work(&i915->rps.work);

	atomic_inc(rps ? &rps->boosts : &i915->rps.boosts);
}

int intel_set_rps(struct drm_i915_private *dev_priv, u8 val)
{
	int err;

	lockdep_assert_held(&dev_priv->rps.hw_lock);
	GEM_BUG_ON(val > dev_priv->rps.max_freq);
	GEM_BUG_ON(val < dev_priv->rps.min_freq);

	if (!dev_priv->rps.enabled) {
		dev_priv->rps.cur_freq = val;
		return 0;
	}

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		err = valleyview_set_rps(dev_priv, val);
	else
		err = gen6_set_rps(dev_priv, val);

	return err;
}

static void gen9_disable_rc6(struct drm_i915_private *dev_priv)
{
	I915_WRITE(GEN6_RC_CONTROL, 0);
	I915_WRITE(GEN9_PG_ENABLE, 0);
}

static void gen9_disable_rps(struct drm_i915_private *dev_priv)
{
	I915_WRITE(GEN6_RP_CONTROL, 0);
}

static void gen6_disable_rps(struct drm_i915_private *dev_priv)
{
	I915_WRITE(GEN6_RC_CONTROL, 0);
	I915_WRITE(GEN6_RPNSWREQ, 1 << 31);
	I915_WRITE(GEN6_RP_CONTROL, 0);
}

static void cherryview_disable_rps(struct drm_i915_private *dev_priv)
{
	I915_WRITE(GEN6_RC_CONTROL, 0);
}

static void valleyview_disable_rps(struct drm_i915_private *dev_priv)
{
	/* we're doing forcewake before Disabling RC6,
	 * This what the BIOS expects when going into suspend */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	I915_WRITE(GEN6_RC_CONTROL, 0);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
}

static void intel_print_rc6_info(struct drm_i915_private *dev_priv, u32 mode)
{
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		if (mode & (GEN7_RC_CTL_TO_MODE | GEN6_RC_CTL_EI_MODE(1)))
			mode = GEN6_RC_CTL_RC6_ENABLE;
		else
			mode = 0;
	}
	if (HAS_RC6p(dev_priv))
		DRM_DEBUG_DRIVER("Enabling RC6 states: "
				 "RC6 %s RC6p %s RC6pp %s\n",
				 onoff(mode & GEN6_RC_CTL_RC6_ENABLE),
				 onoff(mode & GEN6_RC_CTL_RC6p_ENABLE),
				 onoff(mode & GEN6_RC_CTL_RC6pp_ENABLE));

	else
		DRM_DEBUG_DRIVER("Enabling RC6 states: RC6 %s\n",
				 onoff(mode & GEN6_RC_CTL_RC6_ENABLE));
}

static bool bxt_check_bios_rc6_setup(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	bool enable_rc6 = true;
	unsigned long rc6_ctx_base;
	u32 rc_ctl;
	int rc_sw_target;

	rc_ctl = I915_READ(GEN6_RC_CONTROL);
	rc_sw_target = (I915_READ(GEN6_RC_STATE) & RC_SW_TARGET_STATE_MASK) >>
		       RC_SW_TARGET_STATE_SHIFT;
	DRM_DEBUG_DRIVER("BIOS enabled RC states: "
			 "HW_CTRL %s HW_RC6 %s SW_TARGET_STATE %x\n",
			 onoff(rc_ctl & GEN6_RC_CTL_HW_ENABLE),
			 onoff(rc_ctl & GEN6_RC_CTL_RC6_ENABLE),
			 rc_sw_target);

	if (!(I915_READ(RC6_LOCATION) & RC6_CTX_IN_DRAM)) {
		DRM_DEBUG_DRIVER("RC6 Base location not set properly.\n");
		enable_rc6 = false;
	}

	/*
	 * The exact context size is not known for BXT, so assume a page size
	 * for this check.
	 */
	rc6_ctx_base = I915_READ(RC6_CTX_BASE) & RC6_CTX_BASE_MASK;
	if (!((rc6_ctx_base >= ggtt->stolen_reserved_base) &&
	      (rc6_ctx_base + PAGE_SIZE <= ggtt->stolen_reserved_base +
					ggtt->stolen_reserved_size))) {
		DRM_DEBUG_DRIVER("RC6 Base address not as expected.\n");
		enable_rc6 = false;
	}

	if (!(((I915_READ(PWRCTX_MAXCNT_RCSUNIT) & IDLE_TIME_MASK) > 1) &&
	      ((I915_READ(PWRCTX_MAXCNT_VCSUNIT0) & IDLE_TIME_MASK) > 1) &&
	      ((I915_READ(PWRCTX_MAXCNT_BCSUNIT) & IDLE_TIME_MASK) > 1) &&
	      ((I915_READ(PWRCTX_MAXCNT_VECSUNIT) & IDLE_TIME_MASK) > 1))) {
		DRM_DEBUG_DRIVER("Engine Idle wait time not set properly.\n");
		enable_rc6 = false;
	}

	if (!I915_READ(GEN8_PUSHBUS_CONTROL) ||
	    !I915_READ(GEN8_PUSHBUS_ENABLE) ||
	    !I915_READ(GEN8_PUSHBUS_SHIFT)) {
		DRM_DEBUG_DRIVER("Pushbus not setup properly.\n");
		enable_rc6 = false;
	}

	if (!I915_READ(GEN6_GFXPAUSE)) {
		DRM_DEBUG_DRIVER("GFX pause not setup properly.\n");
		enable_rc6 = false;
	}

	if (!I915_READ(GEN8_MISC_CTRL0)) {
		DRM_DEBUG_DRIVER("GPM control not setup properly.\n");
		enable_rc6 = false;
	}

	return enable_rc6;
}

int sanitize_rc6_option(struct drm_i915_private *dev_priv, int enable_rc6)
{
	/* No RC6 before Ironlake and code is gone for ilk. */
	if (INTEL_INFO(dev_priv)->gen < 6)
		return 0;

	if (!enable_rc6)
		return 0;

	if (IS_GEN9_LP(dev_priv) && !bxt_check_bios_rc6_setup(dev_priv)) {
		DRM_INFO("RC6 disabled by BIOS\n");
		return 0;
	}

	/* Respect the kernel parameter if it is set */
	if (enable_rc6 >= 0) {
		int mask;

		if (HAS_RC6p(dev_priv))
			mask = INTEL_RC6_ENABLE | INTEL_RC6p_ENABLE |
			       INTEL_RC6pp_ENABLE;
		else
			mask = INTEL_RC6_ENABLE;

		if ((enable_rc6 & mask) != enable_rc6)
			DRM_DEBUG_DRIVER("Adjusting RC6 mask to %d "
					 "(requested %d, valid %d)\n",
					 enable_rc6 & mask, enable_rc6, mask);

		return enable_rc6 & mask;
	}

	if (IS_IVYBRIDGE(dev_priv))
		return (INTEL_RC6_ENABLE | INTEL_RC6p_ENABLE);

	return INTEL_RC6_ENABLE;
}

static void gen6_init_rps_frequencies(struct drm_i915_private *dev_priv)
{
	/* All of these values are in units of 50MHz */

	/* static values from HW: RP0 > RP1 > RPn (min_freq) */
	if (IS_GEN9_LP(dev_priv)) {
		u32 rp_state_cap = I915_READ(BXT_RP_STATE_CAP);
		dev_priv->rps.rp0_freq = (rp_state_cap >> 16) & 0xff;
		dev_priv->rps.rp1_freq = (rp_state_cap >>  8) & 0xff;
		dev_priv->rps.min_freq = (rp_state_cap >>  0) & 0xff;
	} else {
		u32 rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
		dev_priv->rps.rp0_freq = (rp_state_cap >>  0) & 0xff;
		dev_priv->rps.rp1_freq = (rp_state_cap >>  8) & 0xff;
		dev_priv->rps.min_freq = (rp_state_cap >> 16) & 0xff;
	}
	/* hw_max = RP0 until we check for overclocking */
	dev_priv->rps.max_freq = dev_priv->rps.rp0_freq;

	dev_priv->rps.efficient_freq = dev_priv->rps.rp1_freq;
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv) ||
	    IS_GEN9_BC(dev_priv) || IS_CANNONLAKE(dev_priv)) {
		u32 ddcc_status = 0;

		if (sandybridge_pcode_read(dev_priv,
					   HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL,
					   &ddcc_status) == 0)
			dev_priv->rps.efficient_freq =
				clamp_t(u8,
					((ddcc_status >> 8) & 0xff),
					dev_priv->rps.min_freq,
					dev_priv->rps.max_freq);
	}

	if (IS_GEN9_BC(dev_priv) || IS_CANNONLAKE(dev_priv)) {
		/* Store the frequency values in 16.66 MHZ units, which is
		 * the natural hardware unit for SKL
		 */
		dev_priv->rps.rp0_freq *= GEN9_FREQ_SCALER;
		dev_priv->rps.rp1_freq *= GEN9_FREQ_SCALER;
		dev_priv->rps.min_freq *= GEN9_FREQ_SCALER;
		dev_priv->rps.max_freq *= GEN9_FREQ_SCALER;
		dev_priv->rps.efficient_freq *= GEN9_FREQ_SCALER;
	}
}

static void reset_rps(struct drm_i915_private *dev_priv,
		      int (*set)(struct drm_i915_private *, u8))
{
	u8 freq = dev_priv->rps.cur_freq;

	/* force a reset */
	dev_priv->rps.power = -1;
	dev_priv->rps.cur_freq = -1;

	if (set(dev_priv, freq))
		DRM_ERROR("Failed to reset RPS to initial values\n");
}

/* See the Gen9_GT_PM_Programming_Guide doc for the below */
static void gen9_enable_rps(struct drm_i915_private *dev_priv)
{
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/* Program defaults and thresholds for RPS*/
	I915_WRITE(GEN6_RC_VIDEO_FREQ,
		GEN9_FREQUENCY(dev_priv->rps.rp1_freq));

	/* 1 second timeout*/
	I915_WRITE(GEN6_RP_DOWN_TIMEOUT,
		GT_INTERVAL_FROM_US(dev_priv, 1000000));

	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 0xa);

	/* Leaning on the below call to gen6_set_rps to program/setup the
	 * Up/Down EI & threshold registers, as well as the RP_CONTROL,
	 * RP_INTERRUPT_LIMITS & RPNSWREQ registers */
	reset_rps(dev_priv, gen6_set_rps);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
}

static void gen9_enable_rc6(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	uint32_t rc6_mask = 0;

	/* 1a: Software RC state - RC0 */
	I915_WRITE(GEN6_RC_STATE, 0);

	/* 1b: Get forcewake during program sequence. Although the driver
	 * hasn't enabled a state yet where we need forcewake, BIOS may have.*/
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/* 2a: Disable RC states. */
	I915_WRITE(GEN6_RC_CONTROL, 0);

	/* 2b: Program RC6 thresholds.*/

	/* WaRsDoubleRc6WrlWithCoarsePowerGating: Doubling WRL only when CPG is enabled */
	if (IS_SKYLAKE(dev_priv))
		I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 108 << 16);
	else
		I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 54 << 16);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000); /* 12500 * 1280ns */
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25); /* 25 * 1280ns */
	for_each_engine(engine, dev_priv, id)
		I915_WRITE(RING_MAX_IDLE(engine->mmio_base), 10);

	if (HAS_GUC(dev_priv))
		I915_WRITE(GUC_MAX_IDLE_COUNT, 0xA);

	I915_WRITE(GEN6_RC_SLEEP, 0);

	/* 2c: Program Coarse Power Gating Policies. */
	I915_WRITE(GEN9_MEDIA_PG_IDLE_HYSTERESIS, 25);
	I915_WRITE(GEN9_RENDER_PG_IDLE_HYSTERESIS, 25);

	/* 3a: Enable RC6 */
	if (intel_enable_rc6() & INTEL_RC6_ENABLE)
		rc6_mask = GEN6_RC_CTL_RC6_ENABLE;
	DRM_INFO("RC6 %s\n", onoff(rc6_mask & GEN6_RC_CTL_RC6_ENABLE));
	I915_WRITE(GEN6_RC6_THRESHOLD, 37500); /* 37.5/125ms per EI */
	I915_WRITE(GEN6_RC_CONTROL,
		   GEN6_RC_CTL_HW_ENABLE | GEN6_RC_CTL_EI_MODE(1) | rc6_mask);

	/*
	 * 3b: Enable Coarse Power Gating only when RC6 is enabled.
	 * WaRsDisableCoarsePowerGating:skl,bxt - Render/Media PG need to be disabled with RC6.
	 */
	if (NEEDS_WaRsDisableCoarsePowerGating(dev_priv))
		I915_WRITE(GEN9_PG_ENABLE, 0);
	else
		I915_WRITE(GEN9_PG_ENABLE, (rc6_mask & GEN6_RC_CTL_RC6_ENABLE) ?
				(GEN9_RENDER_PG_ENABLE | GEN9_MEDIA_PG_ENABLE) : 0);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
}

static void gen8_enable_rps(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	uint32_t rc6_mask = 0;

	/* 1a: Software RC state - RC0 */
	I915_WRITE(GEN6_RC_STATE, 0);

	/* 1c & 1d: Get forcewake during program sequence. Although the driver
	 * hasn't enabled a state yet where we need forcewake, BIOS may have.*/
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/* 2a: Disable RC states. */
	I915_WRITE(GEN6_RC_CONTROL, 0);

	/* 2b: Program RC6 thresholds.*/
	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 40 << 16);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000); /* 12500 * 1280ns */
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25); /* 25 * 1280ns */
	for_each_engine(engine, dev_priv, id)
		I915_WRITE(RING_MAX_IDLE(engine->mmio_base), 10);
	I915_WRITE(GEN6_RC_SLEEP, 0);
	if (IS_BROADWELL(dev_priv))
		I915_WRITE(GEN6_RC6_THRESHOLD, 625); /* 800us/1.28 for TO */
	else
		I915_WRITE(GEN6_RC6_THRESHOLD, 50000); /* 50/125ms per EI */

	/* 3: Enable RC6 */
	if (intel_enable_rc6() & INTEL_RC6_ENABLE)
		rc6_mask = GEN6_RC_CTL_RC6_ENABLE;
	intel_print_rc6_info(dev_priv, rc6_mask);
	if (IS_BROADWELL(dev_priv))
		I915_WRITE(GEN6_RC_CONTROL, GEN6_RC_CTL_HW_ENABLE |
				GEN7_RC_CTL_TO_MODE |
				rc6_mask);
	else
		I915_WRITE(GEN6_RC_CONTROL, GEN6_RC_CTL_HW_ENABLE |
				GEN6_RC_CTL_EI_MODE(1) |
				rc6_mask);

	/* 4 Program defaults and thresholds for RPS*/
	I915_WRITE(GEN6_RPNSWREQ,
		   HSW_FREQUENCY(dev_priv->rps.rp1_freq));
	I915_WRITE(GEN6_RC_VIDEO_FREQ,
		   HSW_FREQUENCY(dev_priv->rps.rp1_freq));
	/* NB: Docs say 1s, and 1000000 - which aren't equivalent */
	I915_WRITE(GEN6_RP_DOWN_TIMEOUT, 100000000 / 128); /* 1 second timeout */

	/* Docs recommend 900MHz, and 300 MHz respectively */
	I915_WRITE(GEN6_RP_INTERRUPT_LIMITS,
		   dev_priv->rps.max_freq_softlimit << 24 |
		   dev_priv->rps.min_freq_softlimit << 16);

	I915_WRITE(GEN6_RP_UP_THRESHOLD, 7600000 / 128); /* 76ms busyness per EI, 90% */
	I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 31300000 / 128); /* 313ms busyness per EI, 70%*/
	I915_WRITE(GEN6_RP_UP_EI, 66000); /* 84.48ms, XXX: random? */
	I915_WRITE(GEN6_RP_DOWN_EI, 350000); /* 448ms, XXX: random? */

	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	/* 5: Enable RPS */
	I915_WRITE(GEN6_RP_CONTROL,
		   GEN6_RP_MEDIA_TURBO |
		   GEN6_RP_MEDIA_HW_NORMAL_MODE |
		   GEN6_RP_MEDIA_IS_GFX |
		   GEN6_RP_ENABLE |
		   GEN6_RP_UP_BUSY_AVG |
		   GEN6_RP_DOWN_IDLE_AVG);

	/* 6: Ring frequency + overclocking (our driver does this later */

	reset_rps(dev_priv, gen6_set_rps);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
}

static void gen6_enable_rps(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 rc6vids, rc6_mask = 0;
	u32 gtfifodbg;
	int rc6_mode;
	int ret;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	/* Here begins a magic sequence of register writes to enable
	 * auto-downclocking.
	 *
	 * Perhaps there might be some value in exposing these to
	 * userspace...
	 */
	I915_WRITE(GEN6_RC_STATE, 0);

	/* Clear the DBG now so we don't confuse earlier errors */
	gtfifodbg = I915_READ(GTFIFODBG);
	if (gtfifodbg) {
		DRM_ERROR("GT fifo had a previous error %x\n", gtfifodbg);
		I915_WRITE(GTFIFODBG, gtfifodbg);
	}

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/* disable the counters and set deterministic thresholds */
	I915_WRITE(GEN6_RC_CONTROL, 0);

	I915_WRITE(GEN6_RC1_WAKE_RATE_LIMIT, 1000 << 16);
	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 40 << 16 | 30);
	I915_WRITE(GEN6_RC6pp_WAKE_RATE_LIMIT, 30);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000);
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25);

	for_each_engine(engine, dev_priv, id)
		I915_WRITE(RING_MAX_IDLE(engine->mmio_base), 10);

	I915_WRITE(GEN6_RC_SLEEP, 0);
	I915_WRITE(GEN6_RC1e_THRESHOLD, 1000);
	if (IS_IVYBRIDGE(dev_priv))
		I915_WRITE(GEN6_RC6_THRESHOLD, 125000);
	else
		I915_WRITE(GEN6_RC6_THRESHOLD, 50000);
	I915_WRITE(GEN6_RC6p_THRESHOLD, 150000);
	I915_WRITE(GEN6_RC6pp_THRESHOLD, 64000); /* unused */

	/* Check if we are enabling RC6 */
	rc6_mode = intel_enable_rc6();
	if (rc6_mode & INTEL_RC6_ENABLE)
		rc6_mask |= GEN6_RC_CTL_RC6_ENABLE;

	/* We don't use those on Haswell */
	if (!IS_HASWELL(dev_priv)) {
		if (rc6_mode & INTEL_RC6p_ENABLE)
			rc6_mask |= GEN6_RC_CTL_RC6p_ENABLE;

		if (rc6_mode & INTEL_RC6pp_ENABLE)
			rc6_mask |= GEN6_RC_CTL_RC6pp_ENABLE;
	}

	intel_print_rc6_info(dev_priv, rc6_mask);

	I915_WRITE(GEN6_RC_CONTROL,
		   rc6_mask |
		   GEN6_RC_CTL_EI_MODE(1) |
		   GEN6_RC_CTL_HW_ENABLE);

	/* Power down if completely idle for over 50ms */
	I915_WRITE(GEN6_RP_DOWN_TIMEOUT, 50000);
	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	reset_rps(dev_priv, gen6_set_rps);

	rc6vids = 0;
	ret = sandybridge_pcode_read(dev_priv, GEN6_PCODE_READ_RC6VIDS, &rc6vids);
	if (IS_GEN6(dev_priv) && ret) {
		DRM_DEBUG_DRIVER("Couldn't check for BIOS workaround\n");
	} else if (IS_GEN6(dev_priv) && (GEN6_DECODE_RC6_VID(rc6vids & 0xff) < 450)) {
		DRM_DEBUG_DRIVER("You should update your BIOS. Correcting minimum rc6 voltage (%dmV->%dmV)\n",
			  GEN6_DECODE_RC6_VID(rc6vids & 0xff), 450);
		rc6vids &= 0xffff00;
		rc6vids |= GEN6_ENCODE_RC6_VID(450);
		ret = sandybridge_pcode_write(dev_priv, GEN6_PCODE_WRITE_RC6VIDS, rc6vids);
		if (ret)
			DRM_ERROR("Couldn't fix incorrect rc6 voltage\n");
	}

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
}

static void gen6_update_ring_freq(struct drm_i915_private *dev_priv)
{
	int min_freq = 15;
	unsigned int gpu_freq;
	unsigned int max_ia_freq, min_ring_freq;
	unsigned int max_gpu_freq, min_gpu_freq;
	int scaling_factor = 180;
	struct cpufreq_policy *policy;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	policy = cpufreq_cpu_get(0);
	if (policy) {
		max_ia_freq = policy->cpuinfo.max_freq;
		cpufreq_cpu_put(policy);
	} else {
		/*
		 * Default to measured freq if none found, PCU will ensure we
		 * don't go over
		 */
		max_ia_freq = tsc_khz;
	}

	/* Convert from kHz to MHz */
	max_ia_freq /= 1000;

	min_ring_freq = I915_READ(DCLK) & 0xf;
	/* convert DDR frequency from units of 266.6MHz to bandwidth */
	min_ring_freq = mult_frac(min_ring_freq, 8, 3);

	if (IS_GEN9_BC(dev_priv) || IS_CANNONLAKE(dev_priv)) {
		/* Convert GT frequency to 50 HZ units */
		min_gpu_freq = dev_priv->rps.min_freq / GEN9_FREQ_SCALER;
		max_gpu_freq = dev_priv->rps.max_freq / GEN9_FREQ_SCALER;
	} else {
		min_gpu_freq = dev_priv->rps.min_freq;
		max_gpu_freq = dev_priv->rps.max_freq;
	}

	/*
	 * For each potential GPU frequency, load a ring frequency we'd like
	 * to use for memory access.  We do this by specifying the IA frequency
	 * the PCU should use as a reference to determine the ring frequency.
	 */
	for (gpu_freq = max_gpu_freq; gpu_freq >= min_gpu_freq; gpu_freq--) {
		int diff = max_gpu_freq - gpu_freq;
		unsigned int ia_freq = 0, ring_freq = 0;

		if (IS_GEN9_BC(dev_priv) || IS_CANNONLAKE(dev_priv)) {
			/*
			 * ring_freq = 2 * GT. ring_freq is in 100MHz units
			 * No floor required for ring frequency on SKL.
			 */
			ring_freq = gpu_freq;
		} else if (INTEL_INFO(dev_priv)->gen >= 8) {
			/* max(2 * GT, DDR). NB: GT is 50MHz units */
			ring_freq = max(min_ring_freq, gpu_freq);
		} else if (IS_HASWELL(dev_priv)) {
			ring_freq = mult_frac(gpu_freq, 5, 4);
			ring_freq = max(min_ring_freq, ring_freq);
			/* leave ia_freq as the default, chosen by cpufreq */
		} else {
			/* On older processors, there is no separate ring
			 * clock domain, so in order to boost the bandwidth
			 * of the ring, we need to upclock the CPU (ia_freq).
			 *
			 * For GPU frequencies less than 750MHz,
			 * just use the lowest ring freq.
			 */
			if (gpu_freq < min_freq)
				ia_freq = 800;
			else
				ia_freq = max_ia_freq - ((diff * scaling_factor) / 2);
			ia_freq = DIV_ROUND_CLOSEST(ia_freq, 100);
		}

		sandybridge_pcode_write(dev_priv,
					GEN6_PCODE_WRITE_MIN_FREQ_TABLE,
					ia_freq << GEN6_PCODE_FREQ_IA_RATIO_SHIFT |
					ring_freq << GEN6_PCODE_FREQ_RING_RATIO_SHIFT |
					gpu_freq);
	}
}

static int cherryview_rps_max_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rp0;

	val = vlv_punit_read(dev_priv, FB_GFX_FMAX_AT_VMAX_FUSE);

	switch (INTEL_INFO(dev_priv)->sseu.eu_total) {
	case 8:
		/* (2 * 4) config */
		rp0 = (val >> FB_GFX_FMAX_AT_VMAX_2SS4EU_FUSE_SHIFT);
		break;
	case 12:
		/* (2 * 6) config */
		rp0 = (val >> FB_GFX_FMAX_AT_VMAX_2SS6EU_FUSE_SHIFT);
		break;
	case 16:
		/* (2 * 8) config */
	default:
		/* Setting (2 * 8) Min RP0 for any other combination */
		rp0 = (val >> FB_GFX_FMAX_AT_VMAX_2SS8EU_FUSE_SHIFT);
		break;
	}

	rp0 = (rp0 & FB_GFX_FREQ_FUSE_MASK);

	return rp0;
}

static int cherryview_rps_rpe_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rpe;

	val = vlv_punit_read(dev_priv, PUNIT_GPU_DUTYCYCLE_REG);
	rpe = (val >> PUNIT_GPU_DUTYCYCLE_RPE_FREQ_SHIFT) & PUNIT_GPU_DUTYCYCLE_RPE_FREQ_MASK;

	return rpe;
}

static int cherryview_rps_guar_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rp1;

	val = vlv_punit_read(dev_priv, FB_GFX_FMAX_AT_VMAX_FUSE);
	rp1 = (val & FB_GFX_FREQ_FUSE_MASK);

	return rp1;
}

static u32 cherryview_rps_min_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rpn;

	val = vlv_punit_read(dev_priv, FB_GFX_FMIN_AT_VMIN_FUSE);
	rpn = ((val >> FB_GFX_FMIN_AT_VMIN_FUSE_SHIFT) &
		       FB_GFX_FREQ_FUSE_MASK);

	return rpn;
}

static int valleyview_rps_guar_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rp1;

	val = vlv_nc_read(dev_priv, IOSF_NC_FB_GFX_FREQ_FUSE);

	rp1 = (val & FB_GFX_FGUARANTEED_FREQ_FUSE_MASK) >> FB_GFX_FGUARANTEED_FREQ_FUSE_SHIFT;

	return rp1;
}

static int valleyview_rps_max_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rp0;

	val = vlv_nc_read(dev_priv, IOSF_NC_FB_GFX_FREQ_FUSE);

	rp0 = (val & FB_GFX_MAX_FREQ_FUSE_MASK) >> FB_GFX_MAX_FREQ_FUSE_SHIFT;
	/* Clamp to max */
	rp0 = min_t(u32, rp0, 0xea);

	return rp0;
}

static int valleyview_rps_rpe_freq(struct drm_i915_private *dev_priv)
{
	u32 val, rpe;

	val = vlv_nc_read(dev_priv, IOSF_NC_FB_GFX_FMAX_FUSE_LO);
	rpe = (val & FB_FMAX_VMIN_FREQ_LO_MASK) >> FB_FMAX_VMIN_FREQ_LO_SHIFT;
	val = vlv_nc_read(dev_priv, IOSF_NC_FB_GFX_FMAX_FUSE_HI);
	rpe |= (val & FB_FMAX_VMIN_FREQ_HI_MASK) << 5;

	return rpe;
}

static int valleyview_rps_min_freq(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = vlv_punit_read(dev_priv, PUNIT_REG_GPU_LFM) & 0xff;
	/*
	 * According to the BYT Punit GPU turbo HAS 1.1.6.3 the minimum value
	 * for the minimum frequency in GPLL mode is 0xc1. Contrary to this on
	 * a BYT-M B0 the above register contains 0xbf. Moreover when setting
	 * a frequency Punit will not allow values below 0xc0. Clamp it 0xc0
	 * to make sure it matches what Punit accepts.
	 */
	return max_t(u32, val, 0xc0);
}

/* Check that the pctx buffer wasn't move under us. */
static void valleyview_check_pctx(struct drm_i915_private *dev_priv)
{
	unsigned long pctx_addr = I915_READ(VLV_PCBR) & ~4095;

	WARN_ON(pctx_addr != dev_priv->mm.stolen_base +
			     dev_priv->vlv_pctx->stolen->start);
}


/* Check that the pcbr address is not empty. */
static void cherryview_check_pctx(struct drm_i915_private *dev_priv)
{
	unsigned long pctx_addr = I915_READ(VLV_PCBR) & ~4095;

	WARN_ON((pctx_addr >> VLV_PCBR_ADDR_SHIFT) == 0);
}

static void cherryview_setup_pctx(struct drm_i915_private *dev_priv)
{
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	unsigned long pctx_paddr, paddr;
	u32 pcbr;
	int pctx_size = 32*1024;

	pcbr = I915_READ(VLV_PCBR);
	if ((pcbr >> VLV_PCBR_ADDR_SHIFT) == 0) {
		DRM_DEBUG_DRIVER("BIOS didn't set up PCBR, fixing up\n");
		paddr = (dev_priv->mm.stolen_base +
			 (ggtt->stolen_size - pctx_size));

		pctx_paddr = (paddr & (~4095));
		I915_WRITE(VLV_PCBR, pctx_paddr);
	}

	DRM_DEBUG_DRIVER("PCBR: 0x%08x\n", I915_READ(VLV_PCBR));
}

static void valleyview_setup_pctx(struct drm_i915_private *dev_priv)
{
	struct drm_i915_gem_object *pctx;
	unsigned long pctx_paddr;
	u32 pcbr;
	int pctx_size = 24*1024;

	pcbr = I915_READ(VLV_PCBR);
	if (pcbr) {
		/* BIOS set it up already, grab the pre-alloc'd space */
		int pcbr_offset;

		pcbr_offset = (pcbr & (~4095)) - dev_priv->mm.stolen_base;
		pctx = i915_gem_object_create_stolen_for_preallocated(dev_priv,
								      pcbr_offset,
								      I915_GTT_OFFSET_NONE,
								      pctx_size);
		goto out;
	}

	DRM_DEBUG_DRIVER("BIOS didn't set up PCBR, fixing up\n");

	/*
	 * From the Gunit register HAS:
	 * The Gfx driver is expected to program this register and ensure
	 * proper allocation within Gfx stolen memory.  For example, this
	 * register should be programmed such than the PCBR range does not
	 * overlap with other ranges, such as the frame buffer, protected
	 * memory, or any other relevant ranges.
	 */
	pctx = i915_gem_object_create_stolen(dev_priv, pctx_size);
	if (!pctx) {
		DRM_DEBUG("not enough stolen space for PCTX, disabling\n");
		goto out;
	}

	pctx_paddr = dev_priv->mm.stolen_base + pctx->stolen->start;
	I915_WRITE(VLV_PCBR, pctx_paddr);

out:
	DRM_DEBUG_DRIVER("PCBR: 0x%08x\n", I915_READ(VLV_PCBR));
	dev_priv->vlv_pctx = pctx;
}

static void valleyview_cleanup_pctx(struct drm_i915_private *dev_priv)
{
	if (WARN_ON(!dev_priv->vlv_pctx))
		return;

	i915_gem_object_put(dev_priv->vlv_pctx);
	dev_priv->vlv_pctx = NULL;
}

static void vlv_init_gpll_ref_freq(struct drm_i915_private *dev_priv)
{
	dev_priv->rps.gpll_ref_freq =
		vlv_get_cck_clock(dev_priv, "GPLL ref",
				  CCK_GPLL_CLOCK_CONTROL,
				  dev_priv->czclk_freq);

	DRM_DEBUG_DRIVER("GPLL reference freq: %d kHz\n",
			 dev_priv->rps.gpll_ref_freq);
}

static void valleyview_init_gt_powersave(struct drm_i915_private *dev_priv)
{
	u32 val;

	valleyview_setup_pctx(dev_priv);

	vlv_init_gpll_ref_freq(dev_priv);

	val = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);
	switch ((val >> 6) & 3) {
	case 0:
	case 1:
		dev_priv->mem_freq = 800;
		break;
	case 2:
		dev_priv->mem_freq = 1066;
		break;
	case 3:
		dev_priv->mem_freq = 1333;
		break;
	}
	DRM_DEBUG_DRIVER("DDR speed: %d MHz\n", dev_priv->mem_freq);

	dev_priv->rps.max_freq = valleyview_rps_max_freq(dev_priv);
	dev_priv->rps.rp0_freq = dev_priv->rps.max_freq;
	DRM_DEBUG_DRIVER("max GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.max_freq),
			 dev_priv->rps.max_freq);

	dev_priv->rps.efficient_freq = valleyview_rps_rpe_freq(dev_priv);
	DRM_DEBUG_DRIVER("RPe GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.efficient_freq),
			 dev_priv->rps.efficient_freq);

	dev_priv->rps.rp1_freq = valleyview_rps_guar_freq(dev_priv);
	DRM_DEBUG_DRIVER("RP1(Guar Freq) GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.rp1_freq),
			 dev_priv->rps.rp1_freq);

	dev_priv->rps.min_freq = valleyview_rps_min_freq(dev_priv);
	DRM_DEBUG_DRIVER("min GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.min_freq),
			 dev_priv->rps.min_freq);
}

static void cherryview_init_gt_powersave(struct drm_i915_private *dev_priv)
{
	u32 val;

	cherryview_setup_pctx(dev_priv);

	vlv_init_gpll_ref_freq(dev_priv);

	mutex_lock(&dev_priv->sb_lock);
	val = vlv_cck_read(dev_priv, CCK_FUSE_REG);
	mutex_unlock(&dev_priv->sb_lock);

	switch ((val >> 2) & 0x7) {
	case 3:
		dev_priv->mem_freq = 2000;
		break;
	default:
		dev_priv->mem_freq = 1600;
		break;
	}
	DRM_DEBUG_DRIVER("DDR speed: %d MHz\n", dev_priv->mem_freq);

	dev_priv->rps.max_freq = cherryview_rps_max_freq(dev_priv);
	dev_priv->rps.rp0_freq = dev_priv->rps.max_freq;
	DRM_DEBUG_DRIVER("max GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.max_freq),
			 dev_priv->rps.max_freq);

	dev_priv->rps.efficient_freq = cherryview_rps_rpe_freq(dev_priv);
	DRM_DEBUG_DRIVER("RPe GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.efficient_freq),
			 dev_priv->rps.efficient_freq);

	dev_priv->rps.rp1_freq = cherryview_rps_guar_freq(dev_priv);
	DRM_DEBUG_DRIVER("RP1(Guar) GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.rp1_freq),
			 dev_priv->rps.rp1_freq);

	dev_priv->rps.min_freq = cherryview_rps_min_freq(dev_priv);
	DRM_DEBUG_DRIVER("min GPU freq: %d MHz (%u)\n",
			 intel_gpu_freq(dev_priv, dev_priv->rps.min_freq),
			 dev_priv->rps.min_freq);

	WARN_ONCE((dev_priv->rps.max_freq |
		   dev_priv->rps.efficient_freq |
		   dev_priv->rps.rp1_freq |
		   dev_priv->rps.min_freq) & 1,
		  "Odd GPU freq values\n");
}

static void valleyview_cleanup_gt_powersave(struct drm_i915_private *dev_priv)
{
	valleyview_cleanup_pctx(dev_priv);
}

static void cherryview_enable_rps(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 gtfifodbg, val, rc6_mode = 0, pcbr;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	gtfifodbg = I915_READ(GTFIFODBG) & ~(GT_FIFO_SBDEDICATE_FREE_ENTRY_CHV |
					     GT_FIFO_FREE_ENTRIES_CHV);
	if (gtfifodbg) {
		DRM_DEBUG_DRIVER("GT fifo had a previous error %x\n",
				 gtfifodbg);
		I915_WRITE(GTFIFODBG, gtfifodbg);
	}

	cherryview_check_pctx(dev_priv);

	/* 1a & 1b: Get forcewake during program sequence. Although the driver
	 * hasn't enabled a state yet where we need forcewake, BIOS may have.*/
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/*  Disable RC states. */
	I915_WRITE(GEN6_RC_CONTROL, 0);

	/* 2a: Program RC6 thresholds.*/
	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 40 << 16);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000); /* 12500 * 1280ns */
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25); /* 25 * 1280ns */

	for_each_engine(engine, dev_priv, id)
		I915_WRITE(RING_MAX_IDLE(engine->mmio_base), 10);
	I915_WRITE(GEN6_RC_SLEEP, 0);

	/* TO threshold set to 500 us ( 0x186 * 1.28 us) */
	I915_WRITE(GEN6_RC6_THRESHOLD, 0x186);

	/* allows RC6 residency counter to work */
	I915_WRITE(VLV_COUNTER_CONTROL,
		   _MASKED_BIT_ENABLE(VLV_COUNT_RANGE_HIGH |
				      VLV_MEDIA_RC6_COUNT_EN |
				      VLV_RENDER_RC6_COUNT_EN));

	/* For now we assume BIOS is allocating and populating the PCBR  */
	pcbr = I915_READ(VLV_PCBR);

	/* 3: Enable RC6 */
	if ((intel_enable_rc6() & INTEL_RC6_ENABLE) &&
	    (pcbr >> VLV_PCBR_ADDR_SHIFT))
		rc6_mode = GEN7_RC_CTL_TO_MODE;

	I915_WRITE(GEN6_RC_CONTROL, rc6_mode);

	/* 4 Program defaults and thresholds for RPS*/
	I915_WRITE(GEN6_RP_DOWN_TIMEOUT, 1000000);
	I915_WRITE(GEN6_RP_UP_THRESHOLD, 59400);
	I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 245000);
	I915_WRITE(GEN6_RP_UP_EI, 66000);
	I915_WRITE(GEN6_RP_DOWN_EI, 350000);

	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	/* 5: Enable RPS */
	I915_WRITE(GEN6_RP_CONTROL,
		   GEN6_RP_MEDIA_HW_NORMAL_MODE |
		   GEN6_RP_MEDIA_IS_GFX |
		   GEN6_RP_ENABLE |
		   GEN6_RP_UP_BUSY_AVG |
		   GEN6_RP_DOWN_IDLE_AVG);

	/* Setting Fixed Bias */
	val = VLV_OVERRIDE_EN |
		  VLV_SOC_TDP_EN |
		  CHV_BIAS_CPU_50_SOC_50;
	vlv_punit_write(dev_priv, VLV_TURBO_SOC_OVERRIDE, val);

	val = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);

	/* RPS code assumes GPLL is used */
	WARN_ONCE((val & GPLLENABLE) == 0, "GPLL not enabled\n");

	DRM_DEBUG_DRIVER("GPLL enabled? %s\n", yesno(val & GPLLENABLE));
	DRM_DEBUG_DRIVER("GPU status: 0x%08x\n", val);

	reset_rps(dev_priv, valleyview_set_rps);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
}

static void valleyview_enable_rps(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 gtfifodbg, val, rc6_mode = 0;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	valleyview_check_pctx(dev_priv);

	gtfifodbg = I915_READ(GTFIFODBG);
	if (gtfifodbg) {
		DRM_DEBUG_DRIVER("GT fifo had a previous error %x\n",
				 gtfifodbg);
		I915_WRITE(GTFIFODBG, gtfifodbg);
	}

	/* If VLV, Forcewake all wells, else re-direct to regular path */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	/*  Disable RC states. */
	I915_WRITE(GEN6_RC_CONTROL, 0);

	I915_WRITE(GEN6_RP_DOWN_TIMEOUT, 1000000);
	I915_WRITE(GEN6_RP_UP_THRESHOLD, 59400);
	I915_WRITE(GEN6_RP_DOWN_THRESHOLD, 245000);
	I915_WRITE(GEN6_RP_UP_EI, 66000);
	I915_WRITE(GEN6_RP_DOWN_EI, 350000);

	I915_WRITE(GEN6_RP_IDLE_HYSTERSIS, 10);

	I915_WRITE(GEN6_RP_CONTROL,
		   GEN6_RP_MEDIA_TURBO |
		   GEN6_RP_MEDIA_HW_NORMAL_MODE |
		   GEN6_RP_MEDIA_IS_GFX |
		   GEN6_RP_ENABLE |
		   GEN6_RP_UP_BUSY_AVG |
		   GEN6_RP_DOWN_IDLE_CONT);

	I915_WRITE(GEN6_RC6_WAKE_RATE_LIMIT, 0x00280000);
	I915_WRITE(GEN6_RC_EVALUATION_INTERVAL, 125000);
	I915_WRITE(GEN6_RC_IDLE_HYSTERSIS, 25);

	for_each_engine(engine, dev_priv, id)
		I915_WRITE(RING_MAX_IDLE(engine->mmio_base), 10);

	I915_WRITE(GEN6_RC6_THRESHOLD, 0x557);

	/* allows RC6 residency counter to work */
	I915_WRITE(VLV_COUNTER_CONTROL,
		   _MASKED_BIT_ENABLE(VLV_COUNT_RANGE_HIGH |
				      VLV_MEDIA_RC0_COUNT_EN |
				      VLV_RENDER_RC0_COUNT_EN |
				      VLV_MEDIA_RC6_COUNT_EN |
				      VLV_RENDER_RC6_COUNT_EN));

	if (intel_enable_rc6() & INTEL_RC6_ENABLE)
		rc6_mode = GEN7_RC_CTL_TO_MODE | VLV_RC_CTL_CTX_RST_PARALLEL;

	intel_print_rc6_info(dev_priv, rc6_mode);

	I915_WRITE(GEN6_RC_CONTROL, rc6_mode);

	/* Setting Fixed Bias */
	val = VLV_OVERRIDE_EN |
		  VLV_SOC_TDP_EN |
		  VLV_BIAS_CPU_125_SOC_875;
	vlv_punit_write(dev_priv, VLV_TURBO_SOC_OVERRIDE, val);

	val = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);

	/* RPS code assumes GPLL is used */
	WARN_ONCE((val & GPLLENABLE) == 0, "GPLL not enabled\n");

	DRM_DEBUG_DRIVER("GPLL enabled? %s\n", yesno(val & GPLLENABLE));
	DRM_DEBUG_DRIVER("GPU status: 0x%08x\n", val);

	reset_rps(dev_priv, valleyview_set_rps);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
}

static unsigned long intel_pxfreq(u32 vidfreq)
{
	unsigned long freq;
	int div = (vidfreq & 0x3f0000) >> 16;
	int post = (vidfreq & 0x3000) >> 12;
	int pre = (vidfreq & 0x7);

	if (!pre)
		return 0;

	freq = ((div * 133333) / ((1<<post) * pre));

	return freq;
}

static const struct cparams {
	u16 i;
	u16 t;
	u16 m;
	u16 c;
} cparams[] = {
	{ 1, 1333, 301, 28664 },
	{ 1, 1066, 294, 24460 },
	{ 1, 800, 294, 25192 },
	{ 0, 1333, 276, 27605 },
	{ 0, 1066, 276, 27605 },
	{ 0, 800, 231, 23784 },
};

static unsigned long __i915_chipset_val(struct drm_i915_private *dev_priv)
{
	u64 total_count, diff, ret;
	u32 count1, count2, count3, m = 0, c = 0;
	unsigned long now = jiffies_to_msecs(jiffies), diff1;
	int i;

	lockdep_assert_held(&mchdev_lock);

	diff1 = now - dev_priv->ips.last_time1;

	/* Prevent division-by-zero if we are asking too fast.
	 * Also, we don't get interesting results if we are polling
	 * faster than once in 10ms, so just return the saved value
	 * in such cases.
	 */
	if (diff1 <= 10)
		return dev_priv->ips.chipset_power;

	count1 = I915_READ(DMIEC);
	count2 = I915_READ(DDREC);
	count3 = I915_READ(CSIEC);

	total_count = count1 + count2 + count3;

	/* FIXME: handle per-counter overflow */
	if (total_count < dev_priv->ips.last_count1) {
		diff = ~0UL - dev_priv->ips.last_count1;
		diff += total_count;
	} else {
		diff = total_count - dev_priv->ips.last_count1;
	}

	for (i = 0; i < ARRAY_SIZE(cparams); i++) {
		if (cparams[i].i == dev_priv->ips.c_m &&
		    cparams[i].t == dev_priv->ips.r_t) {
			m = cparams[i].m;
			c = cparams[i].c;
			break;
		}
	}

	diff = div_u64(diff, diff1);
	ret = ((m * diff) + c);
	ret = div_u64(ret, 10);

	dev_priv->ips.last_count1 = total_count;
	dev_priv->ips.last_time1 = now;

	dev_priv->ips.chipset_power = ret;

	return ret;
}

unsigned long i915_chipset_val(struct drm_i915_private *dev_priv)
{
	unsigned long val;

	if (INTEL_INFO(dev_priv)->gen != 5)
		return 0;

	spin_lock_irq(&mchdev_lock);

	val = __i915_chipset_val(dev_priv);

	spin_unlock_irq(&mchdev_lock);

	return val;
}

unsigned long i915_mch_val(struct drm_i915_private *dev_priv)
{
	unsigned long m, x, b;
	u32 tsfs;

	tsfs = I915_READ(TSFS);

	m = ((tsfs & TSFS_SLOPE_MASK) >> TSFS_SLOPE_SHIFT);
	x = I915_READ8(TR1);

	b = tsfs & TSFS_INTR_MASK;

	return ((m * x) / 127) - b;
}

static int _pxvid_to_vd(u8 pxvid)
{
	if (pxvid == 0)
		return 0;

	if (pxvid >= 8 && pxvid < 31)
		pxvid = 31;

	return (pxvid + 2) * 125;
}

static u32 pvid_to_extvid(struct drm_i915_private *dev_priv, u8 pxvid)
{
	const int vd = _pxvid_to_vd(pxvid);
	const int vm = vd - 1125;

	if (INTEL_INFO(dev_priv)->is_mobile)
		return vm > 0 ? vm : 0;

	return vd;
}

static void __i915_update_gfx_val(struct drm_i915_private *dev_priv)
{
	u64 now, diff, diffms;
	u32 count;

	lockdep_assert_held(&mchdev_lock);

	now = ktime_get_raw_ns();
	diffms = now - dev_priv->ips.last_time2;
	do_div(diffms, NSEC_PER_MSEC);

	/* Don't divide by 0 */
	if (!diffms)
		return;

	count = I915_READ(GFXEC);

	if (count < dev_priv->ips.last_count2) {
		diff = ~0UL - dev_priv->ips.last_count2;
		diff += count;
	} else {
		diff = count - dev_priv->ips.last_count2;
	}

	dev_priv->ips.last_count2 = count;
	dev_priv->ips.last_time2 = now;

	/* More magic constants... */
	diff = diff * 1181;
	diff = div_u64(diff, diffms * 10);
	dev_priv->ips.gfx_power = diff;
}

void i915_update_gfx_val(struct drm_i915_private *dev_priv)
{
	if (INTEL_INFO(dev_priv)->gen != 5)
		return;

	spin_lock_irq(&mchdev_lock);

	__i915_update_gfx_val(dev_priv);

	spin_unlock_irq(&mchdev_lock);
}

static unsigned long __i915_gfx_val(struct drm_i915_private *dev_priv)
{
	unsigned long t, corr, state1, corr2, state2;
	u32 pxvid, ext_v;

	lockdep_assert_held(&mchdev_lock);

	pxvid = I915_READ(PXVFREQ(dev_priv->rps.cur_freq));
	pxvid = (pxvid >> 24) & 0x7f;
	ext_v = pvid_to_extvid(dev_priv, pxvid);

	state1 = ext_v;

	t = i915_mch_val(dev_priv);

	/* Revel in the empirically derived constants */

	/* Correction factor in 1/100000 units */
	if (t > 80)
		corr = ((t * 2349) + 135940);
	else if (t >= 50)
		corr = ((t * 964) + 29317);
	else /* < 50 */
		corr = ((t * 301) + 1004);

	corr = corr * ((150142 * state1) / 10000 - 78642);
	corr /= 100000;
	corr2 = (corr * dev_priv->ips.corr);

	state2 = (corr2 * state1) / 10000;
	state2 /= 100; /* convert to mW */

	__i915_update_gfx_val(dev_priv);

	return dev_priv->ips.gfx_power + state2;
}

unsigned long i915_gfx_val(struct drm_i915_private *dev_priv)
{
	unsigned long val;

	if (INTEL_INFO(dev_priv)->gen != 5)
		return 0;

	spin_lock_irq(&mchdev_lock);

	val = __i915_gfx_val(dev_priv);

	spin_unlock_irq(&mchdev_lock);

	return val;
}

/**
 * i915_read_mch_val - return value for IPS use
 *
 * Calculate and return a value for the IPS driver to use when deciding whether
 * we have thermal and power headroom to increase CPU or GPU power budget.
 */
unsigned long i915_read_mch_val(void)
{
	struct drm_i915_private *dev_priv;
	unsigned long chipset_val, graphics_val, ret = 0;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev)
		goto out_unlock;
	dev_priv = i915_mch_dev;

	chipset_val = __i915_chipset_val(dev_priv);
	graphics_val = __i915_gfx_val(dev_priv);

	ret = chipset_val + graphics_val;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_read_mch_val);

/**
 * i915_gpu_raise - raise GPU frequency limit
 *
 * Raise the limit; IPS indicates we have thermal headroom.
 */
bool i915_gpu_raise(void)
{
	struct drm_i915_private *dev_priv;
	bool ret = true;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev) {
		ret = false;
		goto out_unlock;
	}
	dev_priv = i915_mch_dev;

	if (dev_priv->ips.max_delay > dev_priv->ips.fmax)
		dev_priv->ips.max_delay--;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_raise);

/**
 * i915_gpu_lower - lower GPU frequency limit
 *
 * IPS indicates we're close to a thermal limit, so throttle back the GPU
 * frequency maximum.
 */
bool i915_gpu_lower(void)
{
	struct drm_i915_private *dev_priv;
	bool ret = true;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev) {
		ret = false;
		goto out_unlock;
	}
	dev_priv = i915_mch_dev;

	if (dev_priv->ips.max_delay < dev_priv->ips.min_delay)
		dev_priv->ips.max_delay++;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_lower);

/**
 * i915_gpu_busy - indicate GPU business to IPS
 *
 * Tell the IPS driver whether or not the GPU is busy.
 */
bool i915_gpu_busy(void)
{
	bool ret = false;

	spin_lock_irq(&mchdev_lock);
	if (i915_mch_dev)
		ret = i915_mch_dev->gt.awake;
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_busy);

/**
 * i915_gpu_turbo_disable - disable graphics turbo
 *
 * Disable graphics turbo by resetting the max frequency and setting the
 * current frequency to the default.
 */
bool i915_gpu_turbo_disable(void)
{
	struct drm_i915_private *dev_priv;
	bool ret = true;

	spin_lock_irq(&mchdev_lock);
	if (!i915_mch_dev) {
		ret = false;
		goto out_unlock;
	}
	dev_priv = i915_mch_dev;

	dev_priv->ips.max_delay = dev_priv->ips.fstart;

	if (!ironlake_set_drps(dev_priv, dev_priv->ips.fstart))
		ret = false;

out_unlock:
	spin_unlock_irq(&mchdev_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i915_gpu_turbo_disable);

/**
 * Tells the intel_ips driver that the i915 driver is now loaded, if
 * IPS got loaded first.
 *
 * This awkward dance is so that neither module has to depend on the
 * other in order for IPS to do the appropriate communication of
 * GPU turbo limits to i915.
 */
static void
ips_ping_for_i915_load(void)
{
	void (*link)(void);

	link = symbol_get(ips_link_to_i915_driver);
	if (link) {
		link();
		symbol_put(ips_link_to_i915_driver);
	}
}

void intel_gpu_ips_init(struct drm_i915_private *dev_priv)
{
	/* We only register the i915 ips part with intel-ips once everything is
	 * set up, to avoid intel-ips sneaking in and reading bogus values. */
	spin_lock_irq(&mchdev_lock);
	i915_mch_dev = dev_priv;
	spin_unlock_irq(&mchdev_lock);

	ips_ping_for_i915_load();
}

void intel_gpu_ips_teardown(void)
{
	spin_lock_irq(&mchdev_lock);
	i915_mch_dev = NULL;
	spin_unlock_irq(&mchdev_lock);
}

static void intel_init_emon(struct drm_i915_private *dev_priv)
{
	u32 lcfuse;
	u8 pxw[16];
	int i;

	/* Disable to program */
	I915_WRITE(ECR, 0);
	POSTING_READ(ECR);

	/* Program energy weights for various events */
	I915_WRITE(SDEW, 0x15040d00);
	I915_WRITE(CSIEW0, 0x007f0000);
	I915_WRITE(CSIEW1, 0x1e220004);
	I915_WRITE(CSIEW2, 0x04000004);

	for (i = 0; i < 5; i++)
		I915_WRITE(PEW(i), 0);
	for (i = 0; i < 3; i++)
		I915_WRITE(DEW(i), 0);

	/* Program P-state weights to account for frequency power adjustment */
	for (i = 0; i < 16; i++) {
		u32 pxvidfreq = I915_READ(PXVFREQ(i));
		unsigned long freq = intel_pxfreq(pxvidfreq);
		unsigned long vid = (pxvidfreq & PXVFREQ_PX_MASK) >>
			PXVFREQ_PX_SHIFT;
		unsigned long val;

		val = vid * vid;
		val *= (freq / 1000);
		val *= 255;
		val /= (127*127*900);
		if (val > 0xff)
			DRM_ERROR("bad pxval: %ld\n", val);
		pxw[i] = val;
	}
	/* Render standby states get 0 weight */
	pxw[14] = 0;
	pxw[15] = 0;

	for (i = 0; i < 4; i++) {
		u32 val = (pxw[i*4] << 24) | (pxw[(i*4)+1] << 16) |
			(pxw[(i*4)+2] << 8) | (pxw[(i*4)+3]);
		I915_WRITE(PXW(i), val);
	}

	/* Adjust magic regs to magic values (more experimental results) */
	I915_WRITE(OGW0, 0);
	I915_WRITE(OGW1, 0);
	I915_WRITE(EG0, 0x00007f00);
	I915_WRITE(EG1, 0x0000000e);
	I915_WRITE(EG2, 0x000e0000);
	I915_WRITE(EG3, 0x68000300);
	I915_WRITE(EG4, 0x42000000);
	I915_WRITE(EG5, 0x00140031);
	I915_WRITE(EG6, 0);
	I915_WRITE(EG7, 0);

	for (i = 0; i < 8; i++)
		I915_WRITE(PXWL(i), 0);

	/* Enable PMON + select events */
	I915_WRITE(ECR, 0x80000019);

	lcfuse = I915_READ(LCFUSE02);

	dev_priv->ips.corr = (lcfuse & LCFUSE_HIV_MASK);
}

void intel_init_gt_powersave(struct drm_i915_private *dev_priv)
{
	/*
	 * RPM depends on RC6 to save restore the GT HW context, so make RC6 a
	 * requirement.
	 */
	if (!i915.enable_rc6) {
		DRM_INFO("RC6 disabled, disabling runtime PM support\n");
		intel_runtime_pm_get(dev_priv);
	}

	mutex_lock(&dev_priv->drm.struct_mutex);
	mutex_lock(&dev_priv->rps.hw_lock);

	/* Initialize RPS limits (for userspace) */
	if (IS_CHERRYVIEW(dev_priv))
		cherryview_init_gt_powersave(dev_priv);
	else if (IS_VALLEYVIEW(dev_priv))
		valleyview_init_gt_powersave(dev_priv);
	else if (INTEL_GEN(dev_priv) >= 6)
		gen6_init_rps_frequencies(dev_priv);

	/* Derive initial user preferences/limits from the hardware limits */
	dev_priv->rps.idle_freq = dev_priv->rps.min_freq;
	dev_priv->rps.cur_freq = dev_priv->rps.idle_freq;

	dev_priv->rps.max_freq_softlimit = dev_priv->rps.max_freq;
	dev_priv->rps.min_freq_softlimit = dev_priv->rps.min_freq;

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
		dev_priv->rps.min_freq_softlimit =
			max_t(int,
			      dev_priv->rps.efficient_freq,
			      intel_freq_opcode(dev_priv, 450));

	/* After setting max-softlimit, find the overclock max freq */
	if (IS_GEN6(dev_priv) ||
	    IS_IVYBRIDGE(dev_priv) || IS_HASWELL(dev_priv)) {
		u32 params = 0;

		sandybridge_pcode_read(dev_priv, GEN6_READ_OC_PARAMS, &params);
		if (params & BIT(31)) { /* OC supported */
			DRM_DEBUG_DRIVER("Overclocking supported, max: %dMHz, overclock: %dMHz\n",
					 (dev_priv->rps.max_freq & 0xff) * 50,
					 (params & 0xff) * 50);
			dev_priv->rps.max_freq = params & 0xff;
		}
	}

	/* Finally allow us to boost to max by default */
	dev_priv->rps.boost_freq = dev_priv->rps.max_freq;

	mutex_unlock(&dev_priv->rps.hw_lock);
	mutex_unlock(&dev_priv->drm.struct_mutex);

	intel_autoenable_gt_powersave(dev_priv);
}

void intel_cleanup_gt_powersave(struct drm_i915_private *dev_priv)
{
	if (IS_VALLEYVIEW(dev_priv))
		valleyview_cleanup_gt_powersave(dev_priv);

	if (!i915.enable_rc6)
		intel_runtime_pm_put(dev_priv);
}

/**
 * intel_suspend_gt_powersave - suspend PM work and helper threads
 * @dev_priv: i915 device
 *
 * We don't want to disable RC6 or other features here, we just want
 * to make sure any work we've queued has finished and won't bother
 * us while we're suspended.
 */
void intel_suspend_gt_powersave(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) < 6)
		return;

	if (cancel_delayed_work_sync(&dev_priv->rps.autoenable_work))
		intel_runtime_pm_put(dev_priv);

	/* gen6_rps_idle() will be called later to disable interrupts */
}

void intel_sanitize_gt_powersave(struct drm_i915_private *dev_priv)
{
	dev_priv->rps.enabled = true; /* force disabling */
	intel_disable_gt_powersave(dev_priv);

	gen6_reset_rps_interrupts(dev_priv);
}

void intel_disable_gt_powersave(struct drm_i915_private *dev_priv)
{
	if (!READ_ONCE(dev_priv->rps.enabled))
		return;

	mutex_lock(&dev_priv->rps.hw_lock);

	if (INTEL_GEN(dev_priv) >= 9) {
		gen9_disable_rc6(dev_priv);
		gen9_disable_rps(dev_priv);
	} else if (IS_CHERRYVIEW(dev_priv)) {
		cherryview_disable_rps(dev_priv);
	} else if (IS_VALLEYVIEW(dev_priv)) {
		valleyview_disable_rps(dev_priv);
	} else if (INTEL_GEN(dev_priv) >= 6) {
		gen6_disable_rps(dev_priv);
	}  else if (IS_IRONLAKE_M(dev_priv)) {
		ironlake_disable_drps(dev_priv);
	}

	dev_priv->rps.enabled = false;
	mutex_unlock(&dev_priv->rps.hw_lock);
}

void intel_enable_gt_powersave(struct drm_i915_private *dev_priv)
{
	/* We shouldn't be disabling as we submit, so this should be less
	 * racy than it appears!
	 */
	if (READ_ONCE(dev_priv->rps.enabled))
		return;

	/* Powersaving is controlled by the host when inside a VM */
	if (intel_vgpu_active(dev_priv))
		return;

	mutex_lock(&dev_priv->rps.hw_lock);

	if (IS_CHERRYVIEW(dev_priv)) {
		cherryview_enable_rps(dev_priv);
	} else if (IS_VALLEYVIEW(dev_priv)) {
		valleyview_enable_rps(dev_priv);
	} else if (INTEL_GEN(dev_priv) >= 9) {
		gen9_enable_rc6(dev_priv);
		gen9_enable_rps(dev_priv);
		if (IS_GEN9_BC(dev_priv) || IS_CANNONLAKE(dev_priv))
			gen6_update_ring_freq(dev_priv);
	} else if (IS_BROADWELL(dev_priv)) {
		gen8_enable_rps(dev_priv);
		gen6_update_ring_freq(dev_priv);
	} else if (INTEL_GEN(dev_priv) >= 6) {
		gen6_enable_rps(dev_priv);
		gen6_update_ring_freq(dev_priv);
	} else if (IS_IRONLAKE_M(dev_priv)) {
		ironlake_enable_drps(dev_priv);
		intel_init_emon(dev_priv);
	}

	WARN_ON(dev_priv->rps.max_freq < dev_priv->rps.min_freq);
	WARN_ON(dev_priv->rps.idle_freq > dev_priv->rps.max_freq);

	WARN_ON(dev_priv->rps.efficient_freq < dev_priv->rps.min_freq);
	WARN_ON(dev_priv->rps.efficient_freq > dev_priv->rps.max_freq);

	dev_priv->rps.enabled = true;
	mutex_unlock(&dev_priv->rps.hw_lock);
}

static void __intel_autoenable_gt_powersave(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), rps.autoenable_work.work);
	struct intel_engine_cs *rcs;
	struct drm_i915_gem_request *req;

	if (READ_ONCE(dev_priv->rps.enabled))
		goto out;

	rcs = dev_priv->engine[RCS];
	if (rcs->last_retired_context)
		goto out;

	if (!rcs->init_context)
		goto out;

	mutex_lock(&dev_priv->drm.struct_mutex);

	req = i915_gem_request_alloc(rcs, dev_priv->kernel_context);
	if (IS_ERR(req))
		goto unlock;

	if (!i915.enable_execlists && i915_switch_context(req) == 0)
		rcs->init_context(req);

	/* Mark the device busy, calling intel_enable_gt_powersave() */
	i915_add_request(req);

unlock:
	mutex_unlock(&dev_priv->drm.struct_mutex);
out:
	intel_runtime_pm_put(dev_priv);
}

void intel_autoenable_gt_powersave(struct drm_i915_private *dev_priv)
{
	if (READ_ONCE(dev_priv->rps.enabled))
		return;

	if (IS_IRONLAKE_M(dev_priv)) {
		ironlake_enable_drps(dev_priv);
		intel_init_emon(dev_priv);
	} else if (INTEL_INFO(dev_priv)->gen >= 6) {
		/*
		 * PCU communication is slow and this doesn't need to be
		 * done at any specific time, so do this out of our fast path
		 * to make resume and init faster.
		 *
		 * We depend on the HW RC6 power context save/restore
		 * mechanism when entering D3 through runtime PM suspend. So
		 * disable RPM until RPS/RC6 is properly setup. We can only
		 * get here via the driver load/system resume/runtime resume
		 * paths, so the _noresume version is enough (and in case of
		 * runtime resume it's necessary).
		 */
		if (queue_delayed_work(dev_priv->wq,
				       &dev_priv->rps.autoenable_work,
				       round_jiffies_up_relative(HZ)))
			intel_runtime_pm_get_noresume(dev_priv);
	}
}

static void ibx_init_clock_gating(struct drm_i915_private *dev_priv)
{
	/*
	 * On Ibex Peak and Cougar Point, we need to disable clock
	 * gating for the panel power sequencer or it will fail to
	 * start up when no ports are active.
	 */
	I915_WRITE(SOUTH_DSPCLK_GATE_D, PCH_DPLSUNIT_CLOCK_GATE_DISABLE);
}

static void g4x_disable_trickle_feed(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		I915_WRITE(DSPCNTR(pipe),
			   I915_READ(DSPCNTR(pipe)) |
			   DISPPLANE_TRICKLE_FEED_DISABLE);

		I915_WRITE(DSPSURF(pipe), I915_READ(DSPSURF(pipe)));
		POSTING_READ(DSPSURF(pipe));
	}
}

static void ilk_init_lp_watermarks(struct drm_i915_private *dev_priv)
{
	I915_WRITE(WM3_LP_ILK, I915_READ(WM3_LP_ILK) & ~WM1_LP_SR_EN);
	I915_WRITE(WM2_LP_ILK, I915_READ(WM2_LP_ILK) & ~WM1_LP_SR_EN);
	I915_WRITE(WM1_LP_ILK, I915_READ(WM1_LP_ILK) & ~WM1_LP_SR_EN);

	/*
	 * Don't touch WM1S_LP_EN here.
	 * Doing so could cause underruns.
	 */
}

static void ironlake_init_clock_gating(struct drm_i915_private *dev_priv)
{
	uint32_t dspclk_gate = ILK_VRHUNIT_CLOCK_GATE_DISABLE;

	/*
	 * Required for FBC
	 * WaFbcDisableDpfcClockGating:ilk
	 */
	dspclk_gate |= ILK_DPFCRUNIT_CLOCK_GATE_DISABLE |
		   ILK_DPFCUNIT_CLOCK_GATE_DISABLE |
		   ILK_DPFDUNIT_CLOCK_GATE_ENABLE;

	I915_WRITE(PCH_3DCGDIS0,
		   MARIUNIT_CLOCK_GATE_DISABLE |
		   SVSMUNIT_CLOCK_GATE_DISABLE);
	I915_WRITE(PCH_3DCGDIS1,
		   VFMUNIT_CLOCK_GATE_DISABLE);

	/*
	 * According to the spec the following bits should be set in
	 * order to enable memory self-refresh
	 * The bit 22/21 of 0x42004
	 * The bit 5 of 0x42020
	 * The bit 15 of 0x45000
	 */
	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   (I915_READ(ILK_DISPLAY_CHICKEN2) |
		    ILK_DPARB_GATE | ILK_VSDPFD_FULL));
	dspclk_gate |= ILK_DPARBUNIT_CLOCK_GATE_ENABLE;
	I915_WRITE(DISP_ARB_CTL,
		   (I915_READ(DISP_ARB_CTL) |
		    DISP_FBC_WM_DIS));

	ilk_init_lp_watermarks(dev_priv);

	/*
	 * Based on the document from hardware guys the following bits
	 * should be set unconditionally in order to enable FBC.
	 * The bit 22 of 0x42000
	 * The bit 22 of 0x42004
	 * The bit 7,8,9 of 0x42020.
	 */
	if (IS_IRONLAKE_M(dev_priv)) {
		/* WaFbcAsynchFlipDisableFbcQueue:ilk */
		I915_WRITE(ILK_DISPLAY_CHICKEN1,
			   I915_READ(ILK_DISPLAY_CHICKEN1) |
			   ILK_FBCQ_DIS);
		I915_WRITE(ILK_DISPLAY_CHICKEN2,
			   I915_READ(ILK_DISPLAY_CHICKEN2) |
			   ILK_DPARB_GATE);
	}

	I915_WRITE(ILK_DSPCLK_GATE_D, dspclk_gate);

	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   I915_READ(ILK_DISPLAY_CHICKEN2) |
		   ILK_ELPIN_409_SELECT);
	I915_WRITE(_3D_CHICKEN2,
		   _3D_CHICKEN2_WM_READ_PIPELINED << 16 |
		   _3D_CHICKEN2_WM_READ_PIPELINED);

	/* WaDisableRenderCachePipelinedFlush:ilk */
	I915_WRITE(CACHE_MODE_0,
		   _MASKED_BIT_ENABLE(CM0_PIPELINED_RENDER_FLUSH_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:ilk */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	g4x_disable_trickle_feed(dev_priv);

	ibx_init_clock_gating(dev_priv);
}

static void cpt_init_clock_gating(struct drm_i915_private *dev_priv)
{
	int pipe;
	uint32_t val;

	/*
	 * On Ibex Peak and Cougar Point, we need to disable clock
	 * gating for the panel power sequencer or it will fail to
	 * start up when no ports are active.
	 */
	I915_WRITE(SOUTH_DSPCLK_GATE_D, PCH_DPLSUNIT_CLOCK_GATE_DISABLE |
		   PCH_DPLUNIT_CLOCK_GATE_DISABLE |
		   PCH_CPUNIT_CLOCK_GATE_DISABLE);
	I915_WRITE(SOUTH_CHICKEN2, I915_READ(SOUTH_CHICKEN2) |
		   DPLS_EDP_PPS_FIX_DIS);
	/* The below fixes the weird display corruption, a few pixels shifted
	 * downward, on (only) LVDS of some HP laptops with IVY.
	 */
	for_each_pipe(dev_priv, pipe) {
		val = I915_READ(TRANS_CHICKEN2(pipe));
		val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
		val &= ~TRANS_CHICKEN2_FDI_POLARITY_REVERSED;
		if (dev_priv->vbt.fdi_rx_polarity_inverted)
			val |= TRANS_CHICKEN2_FDI_POLARITY_REVERSED;
		val &= ~TRANS_CHICKEN2_FRAME_START_DELAY_MASK;
		val &= ~TRANS_CHICKEN2_DISABLE_DEEP_COLOR_COUNTER;
		val &= ~TRANS_CHICKEN2_DISABLE_DEEP_COLOR_MODESWITCH;
		I915_WRITE(TRANS_CHICKEN2(pipe), val);
	}
	/* WADP0ClockGatingDisable */
	for_each_pipe(dev_priv, pipe) {
		I915_WRITE(TRANS_CHICKEN1(pipe),
			   TRANS_CHICKEN1_DP0UNIT_GC_DISABLE);
	}
}

static void gen6_check_mch_setup(struct drm_i915_private *dev_priv)
{
	uint32_t tmp;

	tmp = I915_READ(MCH_SSKPD);
	if ((tmp & MCH_SSKPD_WM0_MASK) != MCH_SSKPD_WM0_VAL)
		DRM_DEBUG_KMS("Wrong MCH_SSKPD value: 0x%08x This can cause underruns.\n",
			      tmp);
}

static void gen6_init_clock_gating(struct drm_i915_private *dev_priv)
{
	uint32_t dspclk_gate = ILK_VRHUNIT_CLOCK_GATE_DISABLE;

	I915_WRITE(ILK_DSPCLK_GATE_D, dspclk_gate);

	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   I915_READ(ILK_DISPLAY_CHICKEN2) |
		   ILK_ELPIN_409_SELECT);

	/* WaDisableHiZPlanesWhenMSAAEnabled:snb */
	I915_WRITE(_3D_CHICKEN,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN_HIZ_PLANE_DISABLE_MSAA_4X_SNB));

	/* WaDisable_RenderCache_OperationalFlush:snb */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/*
	 * BSpec recoomends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN6_GT_MODE,
		   _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4));

	ilk_init_lp_watermarks(dev_priv);

	I915_WRITE(CACHE_MODE_0,
		   _MASKED_BIT_DISABLE(CM0_STC_EVICT_DISABLE_LRA_SNB));

	I915_WRITE(GEN6_UCGCTL1,
		   I915_READ(GEN6_UCGCTL1) |
		   GEN6_BLBUNIT_CLOCK_GATE_DISABLE |
		   GEN6_CSUNIT_CLOCK_GATE_DISABLE);

	/* According to the BSpec vol1g, bit 12 (RCPBUNIT) clock
	 * gating disable must be set.  Failure to set it results in
	 * flickering pixels due to Z write ordering failures after
	 * some amount of runtime in the Mesa "fire" demo, and Unigine
	 * Sanctuary and Tropics, and apparently anything else with
	 * alpha test or pixel discard.
	 *
	 * According to the spec, bit 11 (RCCUNIT) must also be set,
	 * but we didn't debug actual testcases to find it out.
	 *
	 * WaDisableRCCUnitClockGating:snb
	 * WaDisableRCPBUnitClockGating:snb
	 */
	I915_WRITE(GEN6_UCGCTL2,
		   GEN6_RCPBUNIT_CLOCK_GATE_DISABLE |
		   GEN6_RCCUNIT_CLOCK_GATE_DISABLE);

	/* WaStripsFansDisableFastClipPerformanceFix:snb */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN3_SF_DISABLE_FASTCLIP_CULL));

	/*
	 * Bspec says:
	 * "This bit must be set if 3DSTATE_CLIP clip mode is set to normal and
	 * 3DSTATE_SF number of SF output attributes is more than 16."
	 */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN3_SF_DISABLE_PIPELINED_ATTR_FETCH));

	/*
	 * According to the spec the following bits should be
	 * set in order to enable memory self-refresh and fbc:
	 * The bit21 and bit22 of 0x42000
	 * The bit21 and bit22 of 0x42004
	 * The bit5 and bit7 of 0x42020
	 * The bit14 of 0x70180
	 * The bit14 of 0x71180
	 *
	 * WaFbcAsynchFlipDisableFbcQueue:snb
	 */
	I915_WRITE(ILK_DISPLAY_CHICKEN1,
		   I915_READ(ILK_DISPLAY_CHICKEN1) |
		   ILK_FBCQ_DIS | ILK_PABSTRETCH_DIS);
	I915_WRITE(ILK_DISPLAY_CHICKEN2,
		   I915_READ(ILK_DISPLAY_CHICKEN2) |
		   ILK_DPARB_GATE | ILK_VSDPFD_FULL);
	I915_WRITE(ILK_DSPCLK_GATE_D,
		   I915_READ(ILK_DSPCLK_GATE_D) |
		   ILK_DPARBUNIT_CLOCK_GATE_ENABLE  |
		   ILK_DPFDUNIT_CLOCK_GATE_ENABLE);

	g4x_disable_trickle_feed(dev_priv);

	cpt_init_clock_gating(dev_priv);

	gen6_check_mch_setup(dev_priv);
}

static void gen7_setup_fixed_func_scheduler(struct drm_i915_private *dev_priv)
{
	uint32_t reg = I915_READ(GEN7_FF_THREAD_MODE);

	/*
	 * WaVSThreadDispatchOverride:ivb,vlv
	 *
	 * This actually overrides the dispatch
	 * mode for all thread types.
	 */
	reg &= ~GEN7_FF_SCHED_MASK;
	reg |= GEN7_FF_TS_SCHED_HW;
	reg |= GEN7_FF_VS_SCHED_HW;
	reg |= GEN7_FF_DS_SCHED_HW;

	I915_WRITE(GEN7_FF_THREAD_MODE, reg);
}

static void lpt_init_clock_gating(struct drm_i915_private *dev_priv)
{
	/*
	 * TODO: this bit should only be enabled when really needed, then
	 * disabled when not needed anymore in order to save power.
	 */
	if (HAS_PCH_LPT_LP(dev_priv))
		I915_WRITE(SOUTH_DSPCLK_GATE_D,
			   I915_READ(SOUTH_DSPCLK_GATE_D) |
			   PCH_LP_PARTITION_LEVEL_DISABLE);

	/* WADPOClockGatingDisable:hsw */
	I915_WRITE(TRANS_CHICKEN1(PIPE_A),
		   I915_READ(TRANS_CHICKEN1(PIPE_A)) |
		   TRANS_CHICKEN1_DP0UNIT_GC_DISABLE);
}

static void lpt_suspend_hw(struct drm_i915_private *dev_priv)
{
	if (HAS_PCH_LPT_LP(dev_priv)) {
		uint32_t val = I915_READ(SOUTH_DSPCLK_GATE_D);

		val &= ~PCH_LP_PARTITION_LEVEL_DISABLE;
		I915_WRITE(SOUTH_DSPCLK_GATE_D, val);
	}
}

static void gen8_set_l3sqc_credits(struct drm_i915_private *dev_priv,
				   int general_prio_credits,
				   int high_prio_credits)
{
	u32 misccpctl;
	u32 val;

	/* WaTempDisableDOPClkGating:bdw */
	misccpctl = I915_READ(GEN7_MISCCPCTL);
	I915_WRITE(GEN7_MISCCPCTL, misccpctl & ~GEN7_DOP_CLOCK_GATE_ENABLE);

	val = I915_READ(GEN8_L3SQCREG1);
	val &= ~L3_PRIO_CREDITS_MASK;
	val |= L3_GENERAL_PRIO_CREDITS(general_prio_credits);
	val |= L3_HIGH_PRIO_CREDITS(high_prio_credits);
	I915_WRITE(GEN8_L3SQCREG1, val);

	/*
	 * Wait at least 100 clocks before re-enabling clock gating.
	 * See the definition of L3SQCREG1 in BSpec.
	 */
	POSTING_READ(GEN8_L3SQCREG1);
	udelay(1);
	I915_WRITE(GEN7_MISCCPCTL, misccpctl);
}

static void kabylake_init_clock_gating(struct drm_i915_private *dev_priv)
{
	gen9_init_clock_gating(dev_priv);

	/* WaDisableSDEUnitClockGating:kbl */
	if (IS_KBL_REVID(dev_priv, 0, KBL_REVID_B0))
		I915_WRITE(GEN8_UCGCTL6, I915_READ(GEN8_UCGCTL6) |
			   GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableGamClockGating:kbl */
	if (IS_KBL_REVID(dev_priv, 0, KBL_REVID_B0))
		I915_WRITE(GEN6_UCGCTL1, I915_READ(GEN6_UCGCTL1) |
			   GEN6_GAMUNIT_CLOCK_GATE_DISABLE);

	/* WaFbcNukeOnHostModify:kbl,cfl */
	I915_WRITE(ILK_DPFC_CHICKEN, I915_READ(ILK_DPFC_CHICKEN) |
		   ILK_DPFC_NUKE_ON_ANY_MODIFICATION);
}

static void skylake_init_clock_gating(struct drm_i915_private *dev_priv)
{
	gen9_init_clock_gating(dev_priv);

	/* WAC6entrylatency:skl */
	I915_WRITE(FBC_LLC_READ_CTRL, I915_READ(FBC_LLC_READ_CTRL) |
		   FBC_LLC_FULLY_OPEN);

	/* WaFbcNukeOnHostModify:skl */
	I915_WRITE(ILK_DPFC_CHICKEN, I915_READ(ILK_DPFC_CHICKEN) |
		   ILK_DPFC_NUKE_ON_ANY_MODIFICATION);
}

static void broadwell_init_clock_gating(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	ilk_init_lp_watermarks(dev_priv);

	/* WaSwitchSolVfFArbitrationPriority:bdw */
	I915_WRITE(GAM_ECOCHK, I915_READ(GAM_ECOCHK) | HSW_ECOCHK_ARB_PRIO_SOL);

	/* WaPsrDPAMaskVBlankInSRD:bdw */
	I915_WRITE(CHICKEN_PAR1_1,
		   I915_READ(CHICKEN_PAR1_1) | DPA_MASK_VBLANK_SRD);

	/* WaPsrDPRSUnmaskVBlankInSRD:bdw */
	for_each_pipe(dev_priv, pipe) {
		I915_WRITE(CHICKEN_PIPESL_1(pipe),
			   I915_READ(CHICKEN_PIPESL_1(pipe)) |
			   BDW_DPRS_MASK_VBLANK_SRD);
	}

	/* WaVSRefCountFullforceMissDisable:bdw */
	/* WaDSRefCountFullforceMissDisable:bdw */
	I915_WRITE(GEN7_FF_THREAD_MODE,
		   I915_READ(GEN7_FF_THREAD_MODE) &
		   ~(GEN8_FF_DS_REF_CNT_FFME | GEN7_FF_VS_REF_CNT_FFME));

	I915_WRITE(GEN6_RC_SLEEP_PSMI_CONTROL,
		   _MASKED_BIT_ENABLE(GEN8_RC_SEMA_IDLE_MSG_DISABLE));

	/* WaDisableSDEUnitClockGating:bdw */
	I915_WRITE(GEN8_UCGCTL6, I915_READ(GEN8_UCGCTL6) |
		   GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/* WaProgramL3SqcReg1Default:bdw */
	gen8_set_l3sqc_credits(dev_priv, 30, 2);

	/*
	 * WaGttCachingOffByDefault:bdw
	 * GTT cache may not work with big pages, so if those
	 * are ever enabled GTT cache may need to be disabled.
	 */
	I915_WRITE(HSW_GTT_CACHE_EN, GTT_CACHE_EN_ALL);

	/* WaKVMNotificationOnConfigChange:bdw */
	I915_WRITE(CHICKEN_PAR2_1, I915_READ(CHICKEN_PAR2_1)
		   | KVM_CONFIG_CHANGE_NOTIFICATION_SELECT);

	lpt_init_clock_gating(dev_priv);

	/* WaDisableDopClockGating:bdw
	 *
	 * Also see the CHICKEN2 write in bdw_init_workarounds() to disable DOP
	 * clock gating.
	 */
	I915_WRITE(GEN6_UCGCTL1,
		   I915_READ(GEN6_UCGCTL1) | GEN6_EU_TCUNIT_CLOCK_GATE_DISABLE);
}

static void haswell_init_clock_gating(struct drm_i915_private *dev_priv)
{
	ilk_init_lp_watermarks(dev_priv);

	/* L3 caching of data atomics doesn't work -- disable it. */
	I915_WRITE(HSW_SCRATCH1, HSW_SCRATCH1_L3_DATA_ATOMICS_DISABLE);
	I915_WRITE(HSW_ROW_CHICKEN3,
		   _MASKED_BIT_ENABLE(HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE));

	/* This is required by WaCatErrorRejectionIssue:hsw */
	I915_WRITE(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
			I915_READ(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG) |
			GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	/* WaVSRefCountFullforceMissDisable:hsw */
	I915_WRITE(GEN7_FF_THREAD_MODE,
		   I915_READ(GEN7_FF_THREAD_MODE) & ~GEN7_FF_VS_REF_CNT_FFME);

	/* WaDisable_RenderCache_OperationalFlush:hsw */
	I915_WRITE(CACHE_MODE_0_GEN7, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/* enable HiZ Raw Stall Optimization */
	I915_WRITE(CACHE_MODE_0_GEN7,
		   _MASKED_BIT_DISABLE(HIZ_RAW_STALL_OPT_DISABLE));

	/* WaDisable4x2SubspanOptimization:hsw */
	I915_WRITE(CACHE_MODE_1,
		   _MASKED_BIT_ENABLE(PIXEL_SUBSPAN_COLLECT_OPT_DISABLE));

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN7_GT_MODE,
		   _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4));

	/* WaSampleCChickenBitEnable:hsw */
	I915_WRITE(HALF_SLICE_CHICKEN3,
		   _MASKED_BIT_ENABLE(HSW_SAMPLE_C_PERFORMANCE));

	/* WaSwitchSolVfFArbitrationPriority:hsw */
	I915_WRITE(GAM_ECOCHK, I915_READ(GAM_ECOCHK) | HSW_ECOCHK_ARB_PRIO_SOL);

	/* WaRsPkgCStateDisplayPMReq:hsw */
	I915_WRITE(CHICKEN_PAR1_1,
		   I915_READ(CHICKEN_PAR1_1) | FORCE_ARB_IDLE_PLANES);

	lpt_init_clock_gating(dev_priv);
}

static void ivybridge_init_clock_gating(struct drm_i915_private *dev_priv)
{
	uint32_t snpcr;

	ilk_init_lp_watermarks(dev_priv);

	I915_WRITE(ILK_DSPCLK_GATE_D, ILK_VRHUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableEarlyCull:ivb */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN_SF_DISABLE_OBJEND_CULL));

	/* WaDisableBackToBackFlipFix:ivb */
	I915_WRITE(IVB_CHICKEN3,
		   CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE |
		   CHICKEN3_DGMG_DONE_FIX_DISABLE);

	/* WaDisablePSDDualDispatchEnable:ivb */
	if (IS_IVB_GT1(dev_priv))
		I915_WRITE(GEN7_HALF_SLICE_CHICKEN1,
			   _MASKED_BIT_ENABLE(GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE));

	/* WaDisable_RenderCache_OperationalFlush:ivb */
	I915_WRITE(CACHE_MODE_0_GEN7, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/* Apply the WaDisableRHWOOptimizationForRenderHang:ivb workaround. */
	I915_WRITE(GEN7_COMMON_SLICE_CHICKEN1,
		   GEN7_CSC1_RHWO_OPT_DISABLE_IN_RCC);

	/* WaApplyL3ControlAndL3ChickenMode:ivb */
	I915_WRITE(GEN7_L3CNTLREG1,
			GEN7_WA_FOR_GEN7_L3_CONTROL);
	I915_WRITE(GEN7_L3_CHICKEN_MODE_REGISTER,
		   GEN7_WA_L3_CHICKEN_MODE);
	if (IS_IVB_GT1(dev_priv))
		I915_WRITE(GEN7_ROW_CHICKEN2,
			   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));
	else {
		/* must write both registers */
		I915_WRITE(GEN7_ROW_CHICKEN2,
			   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));
		I915_WRITE(GEN7_ROW_CHICKEN2_GT2,
			   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));
	}

	/* WaForceL3Serialization:ivb */
	I915_WRITE(GEN7_L3SQCREG4, I915_READ(GEN7_L3SQCREG4) &
		   ~L3SQ_URB_READ_CAM_MATCH_DISABLE);

	/*
	 * According to the spec, bit 13 (RCZUNIT) must be set on IVB.
	 * This implements the WaDisableRCZUnitClockGating:ivb workaround.
	 */
	I915_WRITE(GEN6_UCGCTL2,
		   GEN6_RCZUNIT_CLOCK_GATE_DISABLE);

	/* This is required by WaCatErrorRejectionIssue:ivb */
	I915_WRITE(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
			I915_READ(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG) |
			GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	g4x_disable_trickle_feed(dev_priv);

	gen7_setup_fixed_func_scheduler(dev_priv);

	if (0) { /* causes HiZ corruption on ivb:gt1 */
		/* enable HiZ Raw Stall Optimization */
		I915_WRITE(CACHE_MODE_0_GEN7,
			   _MASKED_BIT_DISABLE(HIZ_RAW_STALL_OPT_DISABLE));
	}

	/* WaDisable4x2SubspanOptimization:ivb */
	I915_WRITE(CACHE_MODE_1,
		   _MASKED_BIT_ENABLE(PIXEL_SUBSPAN_COLLECT_OPT_DISABLE));

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN7_GT_MODE,
		   _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4));

	snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);
	snpcr &= ~GEN6_MBC_SNPCR_MASK;
	snpcr |= GEN6_MBC_SNPCR_MED;
	I915_WRITE(GEN6_MBCUNIT_SNPCR, snpcr);

	if (!HAS_PCH_NOP(dev_priv))
		cpt_init_clock_gating(dev_priv);

	gen6_check_mch_setup(dev_priv);
}

static void valleyview_init_clock_gating(struct drm_i915_private *dev_priv)
{
	/* WaDisableEarlyCull:vlv */
	I915_WRITE(_3D_CHICKEN3,
		   _MASKED_BIT_ENABLE(_3D_CHICKEN_SF_DISABLE_OBJEND_CULL));

	/* WaDisableBackToBackFlipFix:vlv */
	I915_WRITE(IVB_CHICKEN3,
		   CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE |
		   CHICKEN3_DGMG_DONE_FIX_DISABLE);

	/* WaPsdDispatchEnable:vlv */
	/* WaDisablePSDDualDispatchEnable:vlv */
	I915_WRITE(GEN7_HALF_SLICE_CHICKEN1,
		   _MASKED_BIT_ENABLE(GEN7_MAX_PS_THREAD_DEP |
				      GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE));

	/* WaDisable_RenderCache_OperationalFlush:vlv */
	I915_WRITE(CACHE_MODE_0_GEN7, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	/* WaForceL3Serialization:vlv */
	I915_WRITE(GEN7_L3SQCREG4, I915_READ(GEN7_L3SQCREG4) &
		   ~L3SQ_URB_READ_CAM_MATCH_DISABLE);

	/* WaDisableDopClockGating:vlv */
	I915_WRITE(GEN7_ROW_CHICKEN2,
		   _MASKED_BIT_ENABLE(DOP_CLOCK_GATING_DISABLE));

	/* This is required by WaCatErrorRejectionIssue:vlv */
	I915_WRITE(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG,
		   I915_READ(GEN7_SQ_CHICKEN_MBCUNIT_CONFIG) |
		   GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB);

	gen7_setup_fixed_func_scheduler(dev_priv);

	/*
	 * According to the spec, bit 13 (RCZUNIT) must be set on IVB.
	 * This implements the WaDisableRCZUnitClockGating:vlv workaround.
	 */
	I915_WRITE(GEN6_UCGCTL2,
		   GEN6_RCZUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableL3Bank2xClockGate:vlv
	 * Disabling L3 clock gating- MMIO 940c[25] = 1
	 * Set bit 25, to disable L3_BANK_2x_CLK_GATING */
	I915_WRITE(GEN7_UCGCTL4,
		   I915_READ(GEN7_UCGCTL4) | GEN7_L3BANK2X_CLOCK_GATE_DISABLE);

	/*
	 * BSpec says this must be set, even though
	 * WaDisable4x2SubspanOptimization isn't listed for VLV.
	 */
	I915_WRITE(CACHE_MODE_1,
		   _MASKED_BIT_ENABLE(PIXEL_SUBSPAN_COLLECT_OPT_DISABLE));

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	I915_WRITE(GEN7_GT_MODE,
		   _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4));

	/*
	 * WaIncreaseL3CreditsForVLVB0:vlv
	 * This is the hardware default actually.
	 */
	I915_WRITE(GEN7_L3SQCREG1, VLV_B0_WA_L3SQCREG1_VALUE);

	/*
	 * WaDisableVLVClockGating_VBIIssue:vlv
	 * Disable clock gating on th GCFG unit to prevent a delay
	 * in the reporting of vblank events.
	 */
	I915_WRITE(VLV_GUNIT_CLOCK_GATE, GCFG_DIS);
}

static void cherryview_init_clock_gating(struct drm_i915_private *dev_priv)
{
	/* WaVSRefCountFullforceMissDisable:chv */
	/* WaDSRefCountFullforceMissDisable:chv */
	I915_WRITE(GEN7_FF_THREAD_MODE,
		   I915_READ(GEN7_FF_THREAD_MODE) &
		   ~(GEN8_FF_DS_REF_CNT_FFME | GEN7_FF_VS_REF_CNT_FFME));

	/* WaDisableSemaphoreAndSyncFlipWait:chv */
	I915_WRITE(GEN6_RC_SLEEP_PSMI_CONTROL,
		   _MASKED_BIT_ENABLE(GEN8_RC_SEMA_IDLE_MSG_DISABLE));

	/* WaDisableCSUnitClockGating:chv */
	I915_WRITE(GEN6_UCGCTL1, I915_READ(GEN6_UCGCTL1) |
		   GEN6_CSUNIT_CLOCK_GATE_DISABLE);

	/* WaDisableSDEUnitClockGating:chv */
	I915_WRITE(GEN8_UCGCTL6, I915_READ(GEN8_UCGCTL6) |
		   GEN8_SDEUNIT_CLOCK_GATE_DISABLE);

	/*
	 * WaProgramL3SqcReg1Default:chv
	 * See gfxspecs/Related Documents/Performance Guide/
	 * LSQC Setting Recommendations.
	 */
	gen8_set_l3sqc_credits(dev_priv, 38, 2);

	/*
	 * GTT cache may not work with big pages, so if those
	 * are ever enabled GTT cache may need to be disabled.
	 */
	I915_WRITE(HSW_GTT_CACHE_EN, GTT_CACHE_EN_ALL);
}

static void g4x_init_clock_gating(struct drm_i915_private *dev_priv)
{
	uint32_t dspclk_gate;

	I915_WRITE(RENCLK_GATE_D1, 0);
	I915_WRITE(RENCLK_GATE_D2, VF_UNIT_CLOCK_GATE_DISABLE |
		   GS_UNIT_CLOCK_GATE_DISABLE |
		   CL_UNIT_CLOCK_GATE_DISABLE);
	I915_WRITE(RAMCLK_GATE_D, 0);
	dspclk_gate = VRHUNIT_CLOCK_GATE_DISABLE |
		OVRUNIT_CLOCK_GATE_DISABLE |
		OVCUNIT_CLOCK_GATE_DISABLE;
	if (IS_GM45(dev_priv))
		dspclk_gate |= DSSUNIT_CLOCK_GATE_DISABLE;
	I915_WRITE(DSPCLK_GATE_D, dspclk_gate);

	/* WaDisableRenderCachePipelinedFlush */
	I915_WRITE(CACHE_MODE_0,
		   _MASKED_BIT_ENABLE(CM0_PIPELINED_RENDER_FLUSH_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:g4x */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));

	g4x_disable_trickle_feed(dev_priv);
}

static void crestline_init_clock_gating(struct drm_i915_private *dev_priv)
{
	I915_WRITE(RENCLK_GATE_D1, I965_RCC_CLOCK_GATE_DISABLE);
	I915_WRITE(RENCLK_GATE_D2, 0);
	I915_WRITE(DSPCLK_GATE_D, 0);
	I915_WRITE(RAMCLK_GATE_D, 0);
	I915_WRITE16(DEUC, 0);
	I915_WRITE(MI_ARB_STATE,
		   _MASKED_BIT_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:gen4 */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));
}

static void broadwater_init_clock_gating(struct drm_i915_private *dev_priv)
{
	I915_WRITE(RENCLK_GATE_D1, I965_RCZ_CLOCK_GATE_DISABLE |
		   I965_RCC_CLOCK_GATE_DISABLE |
		   I965_RCPB_CLOCK_GATE_DISABLE |
		   I965_ISC_CLOCK_GATE_DISABLE |
		   I965_FBC_CLOCK_GATE_DISABLE);
	I915_WRITE(RENCLK_GATE_D2, 0);
	I915_WRITE(MI_ARB_STATE,
		   _MASKED_BIT_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));

	/* WaDisable_RenderCache_OperationalFlush:gen4 */
	I915_WRITE(CACHE_MODE_0, _MASKED_BIT_DISABLE(RC_OP_FLUSH_ENABLE));
}

static void gen3_init_clock_gating(struct drm_i915_private *dev_priv)
{
	u32 dstate = I915_READ(D_STATE);

	dstate |= DSTATE_PLL_D3_OFF | DSTATE_GFX_CLOCK_GATING |
		DSTATE_DOT_CLOCK_GATING;
	I915_WRITE(D_STATE, dstate);

	if (IS_PINEVIEW(dev_priv))
		I915_WRITE(ECOSKPD, _MASKED_BIT_ENABLE(ECO_GATING_CX_ONLY));

	/* IIR "flip pending" means done if this bit is set */
	I915_WRITE(ECOSKPD, _MASKED_BIT_DISABLE(ECO_FLIP_DONE));

	/* interrupts should cause a wake up from C3 */
	I915_WRITE(INSTPM, _MASKED_BIT_ENABLE(INSTPM_AGPBUSY_INT_EN));

	/* On GEN3 we really need to make sure the ARB C3 LP bit is set */
	I915_WRITE(MI_ARB_STATE, _MASKED_BIT_ENABLE(MI_ARB_C3_LP_WRITE_ENABLE));

	I915_WRITE(MI_ARB_STATE,
		   _MASKED_BIT_ENABLE(MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE));
}

static void i85x_init_clock_gating(struct drm_i915_private *dev_priv)
{
	I915_WRITE(RENCLK_GATE_D1, SV_CLOCK_GATE_DISABLE);

	/* interrupts should cause a wake up from C3 */
	I915_WRITE(MI_STATE, _MASKED_BIT_ENABLE(MI_AGPBUSY_INT_EN) |
		   _MASKED_BIT_DISABLE(MI_AGPBUSY_830_MODE));

	I915_WRITE(MEM_MODE,
		   _MASKED_BIT_ENABLE(MEM_DISPLAY_TRICKLE_FEED_DISABLE));
}

static void i830_init_clock_gating(struct drm_i915_private *dev_priv)
{
	I915_WRITE(MEM_MODE,
		   _MASKED_BIT_ENABLE(MEM_DISPLAY_A_TRICKLE_FEED_DISABLE) |
		   _MASKED_BIT_ENABLE(MEM_DISPLAY_B_TRICKLE_FEED_DISABLE));
}

void intel_init_clock_gating(struct drm_i915_private *dev_priv)
{
	dev_priv->display.init_clock_gating(dev_priv);
}

void intel_suspend_hw(struct drm_i915_private *dev_priv)
{
	if (HAS_PCH_LPT(dev_priv))
		lpt_suspend_hw(dev_priv);
}

static void nop_init_clock_gating(struct drm_i915_private *dev_priv)
{
	DRM_DEBUG_KMS("No clock gating settings or workarounds applied.\n");
}

/**
 * intel_init_clock_gating_hooks - setup the clock gating hooks
 * @dev_priv: device private
 *
 * Setup the hooks that configure which clocks of a given platform can be
 * gated and also apply various GT and display specific workarounds for these
 * platforms. Note that some GT specific workarounds are applied separately
 * when GPU contexts or batchbuffers start their execution.
 */
void intel_init_clock_gating_hooks(struct drm_i915_private *dev_priv)
{
	if (IS_SKYLAKE(dev_priv))
		dev_priv->display.init_clock_gating = skylake_init_clock_gating;
	else if (IS_KABYLAKE(dev_priv) || IS_COFFEELAKE(dev_priv))
		dev_priv->display.init_clock_gating = kabylake_init_clock_gating;
	else if (IS_BROXTON(dev_priv))
		dev_priv->display.init_clock_gating = bxt_init_clock_gating;
	else if (IS_GEMINILAKE(dev_priv))
		dev_priv->display.init_clock_gating = glk_init_clock_gating;
	else if (IS_BROADWELL(dev_priv))
		dev_priv->display.init_clock_gating = broadwell_init_clock_gating;
	else if (IS_CHERRYVIEW(dev_priv))
		dev_priv->display.init_clock_gating = cherryview_init_clock_gating;
	else if (IS_HASWELL(dev_priv))
		dev_priv->display.init_clock_gating = haswell_init_clock_gating;
	else if (IS_IVYBRIDGE(dev_priv))
		dev_priv->display.init_clock_gating = ivybridge_init_clock_gating;
	else if (IS_VALLEYVIEW(dev_priv))
		dev_priv->display.init_clock_gating = valleyview_init_clock_gating;
	else if (IS_GEN6(dev_priv))
		dev_priv->display.init_clock_gating = gen6_init_clock_gating;
	else if (IS_GEN5(dev_priv))
		dev_priv->display.init_clock_gating = ironlake_init_clock_gating;
	else if (IS_G4X(dev_priv))
		dev_priv->display.init_clock_gating = g4x_init_clock_gating;
	else if (IS_I965GM(dev_priv))
		dev_priv->display.init_clock_gating = crestline_init_clock_gating;
	else if (IS_I965G(dev_priv))
		dev_priv->display.init_clock_gating = broadwater_init_clock_gating;
	else if (IS_GEN3(dev_priv))
		dev_priv->display.init_clock_gating = gen3_init_clock_gating;
	else if (IS_I85X(dev_priv) || IS_I865G(dev_priv))
		dev_priv->display.init_clock_gating = i85x_init_clock_gating;
	else if (IS_GEN2(dev_priv))
		dev_priv->display.init_clock_gating = i830_init_clock_gating;
	else {
		MISSING_CASE(INTEL_DEVID(dev_priv));
		dev_priv->display.init_clock_gating = nop_init_clock_gating;
	}
}

/* Set up chip specific power management-related functions */
void intel_init_pm(struct drm_i915_private *dev_priv)
{
	intel_fbc_init(dev_priv);

	/* For cxsr */
	if (IS_PINEVIEW(dev_priv))
		i915_pineview_get_mem_freq(dev_priv);
	else if (IS_GEN5(dev_priv))
		i915_ironlake_get_mem_freq(dev_priv);

	/* For FIFO watermark updates */
	if (INTEL_GEN(dev_priv) >= 9) {
		skl_setup_wm_latency(dev_priv);
		dev_priv->display.initial_watermarks = skl_initial_wm;
		dev_priv->display.atomic_update_watermarks = skl_atomic_update_crtc_wm;
		dev_priv->display.compute_global_watermarks = skl_compute_wm;
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		ilk_setup_wm_latency(dev_priv);

		if ((IS_GEN5(dev_priv) && dev_priv->wm.pri_latency[1] &&
		     dev_priv->wm.spr_latency[1] && dev_priv->wm.cur_latency[1]) ||
		    (!IS_GEN5(dev_priv) && dev_priv->wm.pri_latency[0] &&
		     dev_priv->wm.spr_latency[0] && dev_priv->wm.cur_latency[0])) {
			dev_priv->display.compute_pipe_wm = ilk_compute_pipe_wm;
			dev_priv->display.compute_intermediate_wm =
				ilk_compute_intermediate_wm;
			dev_priv->display.initial_watermarks =
				ilk_initial_watermarks;
			dev_priv->display.optimize_watermarks =
				ilk_optimize_watermarks;
		} else {
			DRM_DEBUG_KMS("Failed to read display plane latency. "
				      "Disable CxSR\n");
		}
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		vlv_setup_wm_latency(dev_priv);
		dev_priv->display.compute_pipe_wm = vlv_compute_pipe_wm;
		dev_priv->display.compute_intermediate_wm = vlv_compute_intermediate_wm;
		dev_priv->display.initial_watermarks = vlv_initial_watermarks;
		dev_priv->display.optimize_watermarks = vlv_optimize_watermarks;
		dev_priv->display.atomic_update_watermarks = vlv_atomic_update_fifo;
	} else if (IS_G4X(dev_priv)) {
		g4x_setup_wm_latency(dev_priv);
		dev_priv->display.compute_pipe_wm = g4x_compute_pipe_wm;
		dev_priv->display.compute_intermediate_wm = g4x_compute_intermediate_wm;
		dev_priv->display.initial_watermarks = g4x_initial_watermarks;
		dev_priv->display.optimize_watermarks = g4x_optimize_watermarks;
	} else if (IS_PINEVIEW(dev_priv)) {
		if (!intel_get_cxsr_latency(IS_PINEVIEW_G(dev_priv),
					    dev_priv->is_ddr3,
					    dev_priv->fsb_freq,
					    dev_priv->mem_freq)) {
			DRM_INFO("failed to find known CxSR latency "
				 "(found ddr%s fsb freq %d, mem freq %d), "
				 "disabling CxSR\n",
				 (dev_priv->is_ddr3 == 1) ? "3" : "2",
				 dev_priv->fsb_freq, dev_priv->mem_freq);
			/* Disable CxSR and never update its watermark again */
			intel_set_memory_cxsr(dev_priv, false);
			dev_priv->display.update_wm = NULL;
		} else
			dev_priv->display.update_wm = pineview_update_wm;
	} else if (IS_GEN4(dev_priv)) {
		dev_priv->display.update_wm = i965_update_wm;
	} else if (IS_GEN3(dev_priv)) {
		dev_priv->display.update_wm = i9xx_update_wm;
		dev_priv->display.get_fifo_size = i9xx_get_fifo_size;
	} else if (IS_GEN2(dev_priv)) {
		if (INTEL_INFO(dev_priv)->num_pipes == 1) {
			dev_priv->display.update_wm = i845_update_wm;
			dev_priv->display.get_fifo_size = i845_get_fifo_size;
		} else {
			dev_priv->display.update_wm = i9xx_update_wm;
			dev_priv->display.get_fifo_size = i830_get_fifo_size;
		}
	} else {
		DRM_ERROR("unexpected fall-through in intel_init_pm\n");
	}
}

static inline int gen6_check_mailbox_status(struct drm_i915_private *dev_priv)
{
	uint32_t flags =
		I915_READ_FW(GEN6_PCODE_MAILBOX) & GEN6_PCODE_ERROR_MASK;

	switch (flags) {
	case GEN6_PCODE_SUCCESS:
		return 0;
	case GEN6_PCODE_UNIMPLEMENTED_CMD:
		return -ENODEV;
	case GEN6_PCODE_ILLEGAL_CMD:
		return -ENXIO;
	case GEN6_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE:
	case GEN7_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE:
		return -EOVERFLOW;
	case GEN6_PCODE_TIMEOUT:
		return -ETIMEDOUT;
	default:
		MISSING_CASE(flags);
		return 0;
	}
}

static inline int gen7_check_mailbox_status(struct drm_i915_private *dev_priv)
{
	uint32_t flags =
		I915_READ_FW(GEN6_PCODE_MAILBOX) & GEN6_PCODE_ERROR_MASK;

	switch (flags) {
	case GEN6_PCODE_SUCCESS:
		return 0;
	case GEN6_PCODE_ILLEGAL_CMD:
		return -ENXIO;
	case GEN7_PCODE_TIMEOUT:
		return -ETIMEDOUT;
	case GEN7_PCODE_ILLEGAL_DATA:
		return -EINVAL;
	case GEN7_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE:
		return -EOVERFLOW;
	default:
		MISSING_CASE(flags);
		return 0;
	}
}

int sandybridge_pcode_read(struct drm_i915_private *dev_priv, u32 mbox, u32 *val)
{
	int status;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	/* GEN6_PCODE_* are outside of the forcewake domain, we can
	 * use te fw I915_READ variants to reduce the amount of work
	 * required when reading/writing.
	 */

	if (I915_READ_FW(GEN6_PCODE_MAILBOX) & GEN6_PCODE_READY) {
		DRM_DEBUG_DRIVER("warning: pcode (read from mbox %x) mailbox access failed for %ps\n",
				 mbox, __builtin_return_address(0));
		return -EAGAIN;
	}

	I915_WRITE_FW(GEN6_PCODE_DATA, *val);
	I915_WRITE_FW(GEN6_PCODE_DATA1, 0);
	I915_WRITE_FW(GEN6_PCODE_MAILBOX, GEN6_PCODE_READY | mbox);

	if (__intel_wait_for_register_fw(dev_priv,
					 GEN6_PCODE_MAILBOX, GEN6_PCODE_READY, 0,
					 500, 0, NULL)) {
		DRM_ERROR("timeout waiting for pcode read (from mbox %x) to finish for %ps\n",
			  mbox, __builtin_return_address(0));
		return -ETIMEDOUT;
	}

	*val = I915_READ_FW(GEN6_PCODE_DATA);
	I915_WRITE_FW(GEN6_PCODE_DATA, 0);

	if (INTEL_GEN(dev_priv) > 6)
		status = gen7_check_mailbox_status(dev_priv);
	else
		status = gen6_check_mailbox_status(dev_priv);

	if (status) {
		DRM_DEBUG_DRIVER("warning: pcode (read from mbox %x) mailbox access failed for %ps: %d\n",
				 mbox, __builtin_return_address(0), status);
		return status;
	}

	return 0;
}

int sandybridge_pcode_write(struct drm_i915_private *dev_priv,
			    u32 mbox, u32 val)
{
	int status;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

	/* GEN6_PCODE_* are outside of the forcewake domain, we can
	 * use te fw I915_READ variants to reduce the amount of work
	 * required when reading/writing.
	 */

	if (I915_READ_FW(GEN6_PCODE_MAILBOX) & GEN6_PCODE_READY) {
		DRM_DEBUG_DRIVER("warning: pcode (write of 0x%08x to mbox %x) mailbox access failed for %ps\n",
				 val, mbox, __builtin_return_address(0));
		return -EAGAIN;
	}

	I915_WRITE_FW(GEN6_PCODE_DATA, val);
	I915_WRITE_FW(GEN6_PCODE_DATA1, 0);
	I915_WRITE_FW(GEN6_PCODE_MAILBOX, GEN6_PCODE_READY | mbox);

	if (__intel_wait_for_register_fw(dev_priv,
					 GEN6_PCODE_MAILBOX, GEN6_PCODE_READY, 0,
					 500, 0, NULL)) {
		DRM_ERROR("timeout waiting for pcode write of 0x%08x to mbox %x to finish for %ps\n",
			  val, mbox, __builtin_return_address(0));
		return -ETIMEDOUT;
	}

	I915_WRITE_FW(GEN6_PCODE_DATA, 0);

	if (INTEL_GEN(dev_priv) > 6)
		status = gen7_check_mailbox_status(dev_priv);
	else
		status = gen6_check_mailbox_status(dev_priv);

	if (status) {
		DRM_DEBUG_DRIVER("warning: pcode (write of 0x%08x to mbox %x) mailbox access failed for %ps: %d\n",
				 val, mbox, __builtin_return_address(0), status);
		return status;
	}

	return 0;
}

static bool skl_pcode_try_request(struct drm_i915_private *dev_priv, u32 mbox,
				  u32 request, u32 reply_mask, u32 reply,
				  u32 *status)
{
	u32 val = request;

	*status = sandybridge_pcode_read(dev_priv, mbox, &val);

	return *status || ((val & reply_mask) == reply);
}

/**
 * skl_pcode_request - send PCODE request until acknowledgment
 * @dev_priv: device private
 * @mbox: PCODE mailbox ID the request is targeted for
 * @request: request ID
 * @reply_mask: mask used to check for request acknowledgment
 * @reply: value used to check for request acknowledgment
 * @timeout_base_ms: timeout for polling with preemption enabled
 *
 * Keep resending the @request to @mbox until PCODE acknowledges it, PCODE
 * reports an error or an overall timeout of @timeout_base_ms+50 ms expires.
 * The request is acknowledged once the PCODE reply dword equals @reply after
 * applying @reply_mask. Polling is first attempted with preemption enabled
 * for @timeout_base_ms and if this times out for another 50 ms with
 * preemption disabled.
 *
 * Returns 0 on success, %-ETIMEDOUT in case of a timeout, <0 in case of some
 * other error as reported by PCODE.
 */
int skl_pcode_request(struct drm_i915_private *dev_priv, u32 mbox, u32 request,
		      u32 reply_mask, u32 reply, int timeout_base_ms)
{
	u32 status;
	int ret;

	WARN_ON(!mutex_is_locked(&dev_priv->rps.hw_lock));

#define COND skl_pcode_try_request(dev_priv, mbox, request, reply_mask, reply, \
				   &status)

	/*
	 * Prime the PCODE by doing a request first. Normally it guarantees
	 * that a subsequent request, at most @timeout_base_ms later, succeeds.
	 * _wait_for() doesn't guarantee when its passed condition is evaluated
	 * first, so send the first request explicitly.
	 */
	if (COND) {
		ret = 0;
		goto out;
	}
	ret = _wait_for(COND, timeout_base_ms * 1000, 10);
	if (!ret)
		goto out;

	/*
	 * The above can time out if the number of requests was low (2 in the
	 * worst case) _and_ PCODE was busy for some reason even after a
	 * (queued) request and @timeout_base_ms delay. As a workaround retry
	 * the poll with preemption disabled to maximize the number of
	 * requests. Increase the timeout from @timeout_base_ms to 50ms to
	 * account for interrupts that could reduce the number of these
	 * requests, and for any quirks of the PCODE firmware that delays
	 * the request completion.
	 */
	DRM_DEBUG_KMS("PCODE timeout, retrying with preemption disabled\n");
	WARN_ON_ONCE(timeout_base_ms > 3);
	preempt_disable();
	ret = wait_for_atomic(COND, 50);
	preempt_enable();

out:
	return ret ? ret : status;
#undef COND
}

static int byt_gpu_freq(struct drm_i915_private *dev_priv, int val)
{
	/*
	 * N = val - 0xb7
	 * Slow = Fast = GPLL ref * N
	 */
	return DIV_ROUND_CLOSEST(dev_priv->rps.gpll_ref_freq * (val - 0xb7), 1000);
}

static int byt_freq_opcode(struct drm_i915_private *dev_priv, int val)
{
	return DIV_ROUND_CLOSEST(1000 * val, dev_priv->rps.gpll_ref_freq) + 0xb7;
}

static int chv_gpu_freq(struct drm_i915_private *dev_priv, int val)
{
	/*
	 * N = val / 2
	 * CU (slow) = CU2x (fast) / 2 = GPLL ref * N / 2
	 */
	return DIV_ROUND_CLOSEST(dev_priv->rps.gpll_ref_freq * val, 2 * 2 * 1000);
}

static int chv_freq_opcode(struct drm_i915_private *dev_priv, int val)
{
	/* CHV needs even values */
	return DIV_ROUND_CLOSEST(2 * 1000 * val, dev_priv->rps.gpll_ref_freq) * 2;
}

int intel_gpu_freq(struct drm_i915_private *dev_priv, int val)
{
	if (INTEL_GEN(dev_priv) >= 9)
		return DIV_ROUND_CLOSEST(val * GT_FREQUENCY_MULTIPLIER,
					 GEN9_FREQ_SCALER);
	else if (IS_CHERRYVIEW(dev_priv))
		return chv_gpu_freq(dev_priv, val);
	else if (IS_VALLEYVIEW(dev_priv))
		return byt_gpu_freq(dev_priv, val);
	else
		return val * GT_FREQUENCY_MULTIPLIER;
}

int intel_freq_opcode(struct drm_i915_private *dev_priv, int val)
{
	if (INTEL_GEN(dev_priv) >= 9)
		return DIV_ROUND_CLOSEST(val * GEN9_FREQ_SCALER,
					 GT_FREQUENCY_MULTIPLIER);
	else if (IS_CHERRYVIEW(dev_priv))
		return chv_freq_opcode(dev_priv, val);
	else if (IS_VALLEYVIEW(dev_priv))
		return byt_freq_opcode(dev_priv, val);
	else
		return DIV_ROUND_CLOSEST(val, GT_FREQUENCY_MULTIPLIER);
}

struct request_boost {
	struct work_struct work;
	struct drm_i915_gem_request *req;
};

static void __intel_rps_boost_work(struct work_struct *work)
{
	struct request_boost *boost = container_of(work, struct request_boost, work);
	struct drm_i915_gem_request *req = boost->req;

	if (!i915_gem_request_completed(req))
		gen6_rps_boost(req, NULL);

	i915_gem_request_put(req);
	kfree(boost);
}

void intel_queue_rps_boost_for_request(struct drm_i915_gem_request *req)
{
	struct request_boost *boost;

	if (req == NULL || INTEL_GEN(req->i915) < 6)
		return;

	if (i915_gem_request_completed(req))
		return;

	boost = kmalloc(sizeof(*boost), GFP_ATOMIC);
	if (boost == NULL)
		return;

	boost->req = i915_gem_request_get(req);

	INIT_WORK(&boost->work, __intel_rps_boost_work);
	queue_work(req->i915->wq, &boost->work);
}

void intel_pm_setup(struct drm_i915_private *dev_priv)
{
	mutex_init(&dev_priv->rps.hw_lock);

	INIT_DELAYED_WORK(&dev_priv->rps.autoenable_work,
			  __intel_autoenable_gt_powersave);
	atomic_set(&dev_priv->rps.num_waiters, 0);

	dev_priv->pm.suspended = false;
	atomic_set(&dev_priv->pm.wakeref_count, 0);
}

static u64 vlv_residency_raw(struct drm_i915_private *dev_priv,
			     const i915_reg_t reg)
{
	u32 lower, upper, tmp;
	int loop = 2;

	/* The register accessed do not need forcewake. We borrow
	 * uncore lock to prevent concurrent access to range reg.
	 */
	spin_lock_irq(&dev_priv->uncore.lock);

	/* vlv and chv residency counters are 40 bits in width.
	 * With a control bit, we can choose between upper or lower
	 * 32bit window into this counter.
	 *
	 * Although we always use the counter in high-range mode elsewhere,
	 * userspace may attempt to read the value before rc6 is initialised,
	 * before we have set the default VLV_COUNTER_CONTROL value. So always
	 * set the high bit to be safe.
	 */
	I915_WRITE_FW(VLV_COUNTER_CONTROL,
		      _MASKED_BIT_ENABLE(VLV_COUNT_RANGE_HIGH));
	upper = I915_READ_FW(reg);
	do {
		tmp = upper;

		I915_WRITE_FW(VLV_COUNTER_CONTROL,
			      _MASKED_BIT_DISABLE(VLV_COUNT_RANGE_HIGH));
		lower = I915_READ_FW(reg);

		I915_WRITE_FW(VLV_COUNTER_CONTROL,
			      _MASKED_BIT_ENABLE(VLV_COUNT_RANGE_HIGH));
		upper = I915_READ_FW(reg);
	} while (upper != tmp && --loop);

	/* Everywhere else we always use VLV_COUNTER_CONTROL with the
	 * VLV_COUNT_RANGE_HIGH bit set - so it is safe to leave it set
	 * now.
	 */

	spin_unlock_irq(&dev_priv->uncore.lock);

	return lower | (u64)upper << 8;
}

u64 intel_rc6_residency_us(struct drm_i915_private *dev_priv,
			   const i915_reg_t reg)
{
	u64 time_hw, units, div;

	if (!intel_enable_rc6())
		return 0;

	intel_runtime_pm_get(dev_priv);

	/* On VLV and CHV, residency time is in CZ units rather than 1.28us */
	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		units = 1000;
		div = dev_priv->czclk_freq;

		time_hw = vlv_residency_raw(dev_priv, reg);
	} else if (IS_GEN9_LP(dev_priv)) {
		units = 1000;
		div = 1200;		/* 833.33ns */

		time_hw = I915_READ(reg);
	} else {
		units = 128000; /* 1.28us */
		div = 100000;

		time_hw = I915_READ(reg);
	}

	intel_runtime_pm_put(dev_priv);
	return DIV_ROUND_UP_ULL(time_hw * units, div);
}
