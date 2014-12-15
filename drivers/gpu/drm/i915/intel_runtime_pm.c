/*
 * Copyright © 2012-2014 Intel Corporation
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
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <linux/pm_runtime.h>
#include <linux/vgaarb.h>

#include "i915_drv.h"
#include "intel_drv.h"
#include <drm/i915_powerwell.h>

/**
 * DOC: runtime pm
 *
 * The i915 driver supports dynamic enabling and disabling of entire hardware
 * blocks at runtime. This is especially important on the display side where
 * software is supposed to control many power gates manually on recent hardware,
 * since on the GT side a lot of the power management is done by the hardware.
 * But even there some manual control at the device level is required.
 *
 * Since i915 supports a diverse set of platforms with a unified codebase and
 * hardware engineers just love to shuffle functionality around between power
 * domains there's a sizeable amount of indirection required. This file provides
 * generic functions to the driver for grabbing and releasing references for
 * abstract power domains. It then maps those to the actual power wells
 * present for a given platform.
 */

static struct i915_power_domains *hsw_pwr;

#define for_each_power_well(i, power_well, domain_mask, power_domains)	\
	for (i = 0;							\
	     i < (power_domains)->power_well_count &&			\
		 ((power_well) = &(power_domains)->power_wells[i]);	\
	     i++)							\
		if ((power_well)->domains & (domain_mask))

#define for_each_power_well_rev(i, power_well, domain_mask, power_domains) \
	for (i = (power_domains)->power_well_count - 1;			 \
	     i >= 0 && ((power_well) = &(power_domains)->power_wells[i]);\
	     i--)							 \
		if ((power_well)->domains & (domain_mask))

/*
 * We should only use the power well if we explicitly asked the hardware to
 * enable it, so check if it's enabled and also check if we've requested it to
 * be enabled.
 */
static bool hsw_power_well_enabled(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	return I915_READ(HSW_PWR_WELL_DRIVER) ==
		     (HSW_PWR_WELL_ENABLE_REQUEST | HSW_PWR_WELL_STATE_ENABLED);
}

/**
 * __intel_display_power_is_enabled - unlocked check for a power domain
 * @dev_priv: i915 device instance
 * @domain: power domain to check
 *
 * This is the unlocked version of intel_display_power_is_enabled() and should
 * only be used from error capture and recovery code where deadlocks are
 * possible.
 *
 * Returns:
 * True when the power domain is enabled, false otherwise.
 */
bool __intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				      enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;
	bool is_enabled;
	int i;

	if (dev_priv->pm.suspended)
		return false;

	power_domains = &dev_priv->power_domains;

	is_enabled = true;

	for_each_power_well_rev(i, power_well, BIT(domain), power_domains) {
		if (power_well->always_on)
			continue;

		if (!power_well->hw_enabled) {
			is_enabled = false;
			break;
		}
	}

	return is_enabled;
}

/**
 * intel_display_power_is_enabled - check for a power domain
 * @dev_priv: i915 device instance
 * @domain: power domain to check
 *
 * This function can be used to check the hw power domain state. It is mostly
 * used in hardware state readout functions. Everywhere else code should rely
 * upon explicit power domain reference counting to ensure that the hardware
 * block is powered up before accessing it.
 *
 * Callers must hold the relevant modesetting locks to ensure that concurrent
 * threads can't disable the power well while the caller tries to read a few
 * registers.
 *
 * Returns:
 * True when the power domain is enabled, false otherwise.
 */
bool intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				    enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	bool ret;

	power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);
	ret = __intel_display_power_is_enabled(dev_priv, domain);
	mutex_unlock(&power_domains->lock);

	return ret;
}

/**
 * intel_display_set_init_power - set the initial power domain state
 * @dev_priv: i915 device instance
 * @enable: whether to enable or disable the initial power domain state
 *
 * For simplicity our driver load/unload and system suspend/resume code assumes
 * that all power domains are always enabled. This functions controls the state
 * of this little hack. While the initial power domain state is enabled runtime
 * pm is effectively disabled.
 */
void intel_display_set_init_power(struct drm_i915_private *dev_priv,
				  bool enable)
{
	if (dev_priv->power_domains.init_power_on == enable)
		return;

	if (enable)
		intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);
	else
		intel_display_power_put(dev_priv, POWER_DOMAIN_INIT);

	dev_priv->power_domains.init_power_on = enable;
}

/*
 * Starting with Haswell, we have a "Power Down Well" that can be turned off
 * when not needed anymore. We have 4 registers that can request the power well
 * to be enabled, and it will only be disabled if none of the registers is
 * requesting it to be enabled.
 */
static void hsw_power_well_post_enable(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	/*
	 * After we re-enable the power well, if we touch VGA register 0x3d5
	 * we'll get unclaimed register interrupts. This stops after we write
	 * anything to the VGA MSR register. The vgacon module uses this
	 * register all the time, so if we unbind our driver and, as a
	 * consequence, bind vgacon, we'll get stuck in an infinite loop at
	 * console_unlock(). So make here we touch the VGA MSR register, making
	 * sure vgacon can keep working normally without triggering interrupts
	 * and error messages.
	 */
	vga_get_uninterruptible(dev->pdev, VGA_RSRC_LEGACY_IO);
	outb(inb(VGA_MSR_READ), VGA_MSR_WRITE);
	vga_put(dev->pdev, VGA_RSRC_LEGACY_IO);

	if (IS_BROADWELL(dev) || (INTEL_INFO(dev)->gen >= 9))
		gen8_irq_power_well_post_enable(dev_priv);
}

static void hsw_set_power_well(struct drm_i915_private *dev_priv,
			       struct i915_power_well *power_well, bool enable)
{
	bool is_enabled, enable_requested;
	uint32_t tmp;

	tmp = I915_READ(HSW_PWR_WELL_DRIVER);
	is_enabled = tmp & HSW_PWR_WELL_STATE_ENABLED;
	enable_requested = tmp & HSW_PWR_WELL_ENABLE_REQUEST;

	if (enable) {
		if (!enable_requested)
			I915_WRITE(HSW_PWR_WELL_DRIVER,
				   HSW_PWR_WELL_ENABLE_REQUEST);

		if (!is_enabled) {
			DRM_DEBUG_KMS("Enabling power well\n");
			if (wait_for((I915_READ(HSW_PWR_WELL_DRIVER) &
				      HSW_PWR_WELL_STATE_ENABLED), 20))
				DRM_ERROR("Timeout enabling power well\n");
			hsw_power_well_post_enable(dev_priv);
		}

	} else {
		if (enable_requested) {
			I915_WRITE(HSW_PWR_WELL_DRIVER, 0);
			POSTING_READ(HSW_PWR_WELL_DRIVER);
			DRM_DEBUG_KMS("Requesting to disable the power well\n");
		}
	}
}

static void hsw_power_well_sync_hw(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	hsw_set_power_well(dev_priv, power_well, power_well->count > 0);

	/*
	 * We're taking over the BIOS, so clear any requests made by it since
	 * the driver is in charge now.
	 */
	if (I915_READ(HSW_PWR_WELL_BIOS) & HSW_PWR_WELL_ENABLE_REQUEST)
		I915_WRITE(HSW_PWR_WELL_BIOS, 0);
}

static void hsw_power_well_enable(struct drm_i915_private *dev_priv,
				  struct i915_power_well *power_well)
{
	hsw_set_power_well(dev_priv, power_well, true);
}

static void hsw_power_well_disable(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	hsw_set_power_well(dev_priv, power_well, false);
}

static void i9xx_always_on_power_well_noop(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
}

static bool i9xx_always_on_power_well_enabled(struct drm_i915_private *dev_priv,
					     struct i915_power_well *power_well)
{
	return true;
}

static void vlv_set_power_well(struct drm_i915_private *dev_priv,
			       struct i915_power_well *power_well, bool enable)
{
	enum punit_power_well power_well_id = power_well->data;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(power_well_id);
	state = enable ? PUNIT_PWRGT_PWR_ON(power_well_id) :
			 PUNIT_PWRGT_PWR_GATE(power_well_id);

	mutex_lock(&dev_priv->rps.hw_lock);

#define COND \
	((vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL);
	ctrl &= ~mask;
	ctrl |= state;
	vlv_punit_write(dev_priv, PUNIT_REG_PWRGT_CTRL, ctrl);

	if (wait_for(COND, 100))
		DRM_ERROR("timout setting power well state %08x (%08x)\n",
			  state,
			  vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL));

#undef COND

out:
	mutex_unlock(&dev_priv->rps.hw_lock);
}

static void vlv_power_well_sync_hw(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	vlv_set_power_well(dev_priv, power_well, power_well->count > 0);
}

static void vlv_power_well_enable(struct drm_i915_private *dev_priv,
				  struct i915_power_well *power_well)
{
	vlv_set_power_well(dev_priv, power_well, true);
}

static void vlv_power_well_disable(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	vlv_set_power_well(dev_priv, power_well, false);
}

static bool vlv_power_well_enabled(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	int power_well_id = power_well->data;
	bool enabled = false;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(power_well_id);
	ctrl = PUNIT_PWRGT_PWR_ON(power_well_id);

	mutex_lock(&dev_priv->rps.hw_lock);

	state = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask;
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	WARN_ON(state != PUNIT_PWRGT_PWR_ON(power_well_id) &&
		state != PUNIT_PWRGT_PWR_GATE(power_well_id));
	if (state == ctrl)
		enabled = true;

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL) & mask;
	WARN_ON(ctrl != state);

	mutex_unlock(&dev_priv->rps.hw_lock);

	return enabled;
}

static void vlv_display_power_well_enable(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DISP2D);

	vlv_set_power_well(dev_priv, power_well, true);

	spin_lock_irq(&dev_priv->irq_lock);
	valleyview_enable_display_irqs(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	/*
	 * During driver initialization/resume we can avoid restoring the
	 * part of the HW/SW state that will be inited anyway explicitly.
	 */
	if (dev_priv->power_domains.initializing)
		return;

	intel_hpd_init(dev_priv);

	i915_redisable_vga_power_on(dev_priv->dev);
}

static void vlv_display_power_well_disable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DISP2D);

	spin_lock_irq(&dev_priv->irq_lock);
	valleyview_disable_display_irqs(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	vlv_set_power_well(dev_priv, power_well, false);

	vlv_power_sequencer_reset(dev_priv);
}

static void vlv_dpio_cmn_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DPIO_CMN_BC);

	/*
	 * Enable the CRI clock source so we can get at the
	 * display and the reference clock for VGA
	 * hotplug / manual detection.
	 */
	I915_WRITE(DPLL(PIPE_B), I915_READ(DPLL(PIPE_B)) |
		   DPLL_REFA_CLK_ENABLE_VLV | DPLL_INTEGRATED_CRI_CLK_VLV);
	udelay(1); /* >10ns for cmnreset, >0ns for sidereset */

	vlv_set_power_well(dev_priv, power_well, true);

	/*
	 * From VLV2A0_DP_eDP_DPIO_driver_vbios_notes_10.docx -
	 *  6.	De-assert cmn_reset/side_reset. Same as VLV X0.
	 *   a.	GUnit 0x2110 bit[0] set to 1 (def 0)
	 *   b.	The other bits such as sfr settings / modesel may all
	 *	be set to 0.
	 *
	 * This should only be done on init and resume from S3 with
	 * both PLLs disabled, or we risk losing DPIO and PLL
	 * synchronization.
	 */
	I915_WRITE(DPIO_CTL, I915_READ(DPIO_CTL) | DPIO_CMNRST);
}

static void vlv_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	enum pipe pipe;

	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DPIO_CMN_BC);

	for_each_pipe(dev_priv, pipe)
		assert_pll_disabled(dev_priv, pipe);

	/* Assert common reset */
	I915_WRITE(DPIO_CTL, I915_READ(DPIO_CTL) & ~DPIO_CMNRST);

	vlv_set_power_well(dev_priv, power_well, false);
}

static void chv_dpio_cmn_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	enum dpio_phy phy;

	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DPIO_CMN_BC &&
		     power_well->data != PUNIT_POWER_WELL_DPIO_CMN_D);

	/*
	 * Enable the CRI clock source so we can get at the
	 * display and the reference clock for VGA
	 * hotplug / manual detection.
	 */
	if (power_well->data == PUNIT_POWER_WELL_DPIO_CMN_BC) {
		phy = DPIO_PHY0;
		I915_WRITE(DPLL(PIPE_B), I915_READ(DPLL(PIPE_B)) |
			   DPLL_REFA_CLK_ENABLE_VLV);
		I915_WRITE(DPLL(PIPE_B), I915_READ(DPLL(PIPE_B)) |
			   DPLL_REFA_CLK_ENABLE_VLV | DPLL_INTEGRATED_CRI_CLK_VLV);
	} else {
		phy = DPIO_PHY1;
		I915_WRITE(DPLL(PIPE_C), I915_READ(DPLL(PIPE_C)) |
			   DPLL_REFA_CLK_ENABLE_VLV | DPLL_INTEGRATED_CRI_CLK_VLV);
	}
	udelay(1); /* >10ns for cmnreset, >0ns for sidereset */
	vlv_set_power_well(dev_priv, power_well, true);

	/* Poll for phypwrgood signal */
	if (wait_for(I915_READ(DISPLAY_PHY_STATUS) & PHY_POWERGOOD(phy), 1))
		DRM_ERROR("Display PHY %d is not power up\n", phy);

	I915_WRITE(DISPLAY_PHY_CONTROL, I915_READ(DISPLAY_PHY_CONTROL) |
		   PHY_COM_LANE_RESET_DEASSERT(phy));
}

static void chv_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	enum dpio_phy phy;

	WARN_ON_ONCE(power_well->data != PUNIT_POWER_WELL_DPIO_CMN_BC &&
		     power_well->data != PUNIT_POWER_WELL_DPIO_CMN_D);

	if (power_well->data == PUNIT_POWER_WELL_DPIO_CMN_BC) {
		phy = DPIO_PHY0;
		assert_pll_disabled(dev_priv, PIPE_A);
		assert_pll_disabled(dev_priv, PIPE_B);
	} else {
		phy = DPIO_PHY1;
		assert_pll_disabled(dev_priv, PIPE_C);
	}

	I915_WRITE(DISPLAY_PHY_CONTROL, I915_READ(DISPLAY_PHY_CONTROL) &
		   ~PHY_COM_LANE_RESET_DEASSERT(phy));

	vlv_set_power_well(dev_priv, power_well, false);
}

static bool chv_pipe_power_well_enabled(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well)
{
	enum pipe pipe = power_well->data;
	bool enabled;
	u32 state, ctrl;

	mutex_lock(&dev_priv->rps.hw_lock);

	state = vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ) & DP_SSS_MASK(pipe);
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	WARN_ON(state != DP_SSS_PWR_ON(pipe) && state != DP_SSS_PWR_GATE(pipe));
	enabled = state == DP_SSS_PWR_ON(pipe);

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ) & DP_SSC_MASK(pipe);
	WARN_ON(ctrl << 16 != state);

	mutex_unlock(&dev_priv->rps.hw_lock);

	return enabled;
}

static void chv_set_pipe_power_well(struct drm_i915_private *dev_priv,
				    struct i915_power_well *power_well,
				    bool enable)
{
	enum pipe pipe = power_well->data;
	u32 state;
	u32 ctrl;

	state = enable ? DP_SSS_PWR_ON(pipe) : DP_SSS_PWR_GATE(pipe);

	mutex_lock(&dev_priv->rps.hw_lock);

#define COND \
	((vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ) & DP_SSS_MASK(pipe)) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ);
	ctrl &= ~DP_SSC_MASK(pipe);
	ctrl |= enable ? DP_SSC_PWR_ON(pipe) : DP_SSC_PWR_GATE(pipe);
	vlv_punit_write(dev_priv, PUNIT_REG_DSPFREQ, ctrl);

	if (wait_for(COND, 100))
		DRM_ERROR("timout setting power well state %08x (%08x)\n",
			  state,
			  vlv_punit_read(dev_priv, PUNIT_REG_DSPFREQ));

#undef COND

out:
	mutex_unlock(&dev_priv->rps.hw_lock);
}

static void chv_pipe_power_well_sync_hw(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well)
{
	chv_set_pipe_power_well(dev_priv, power_well, power_well->count > 0);
}

static void chv_pipe_power_well_enable(struct drm_i915_private *dev_priv,
				       struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PIPE_A &&
		     power_well->data != PIPE_B &&
		     power_well->data != PIPE_C);

	chv_set_pipe_power_well(dev_priv, power_well, true);

	if (power_well->data == PIPE_A) {
		spin_lock_irq(&dev_priv->irq_lock);
		valleyview_enable_display_irqs(dev_priv);
		spin_unlock_irq(&dev_priv->irq_lock);

		/*
		 * During driver initialization/resume we can avoid restoring the
		 * part of the HW/SW state that will be inited anyway explicitly.
		 */
		if (dev_priv->power_domains.initializing)
			return;

		intel_hpd_init(dev_priv);

		i915_redisable_vga_power_on(dev_priv->dev);
	}
}

static void chv_pipe_power_well_disable(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well)
{
	WARN_ON_ONCE(power_well->data != PIPE_A &&
		     power_well->data != PIPE_B &&
		     power_well->data != PIPE_C);

	if (power_well->data == PIPE_A) {
		spin_lock_irq(&dev_priv->irq_lock);
		valleyview_disable_display_irqs(dev_priv);
		spin_unlock_irq(&dev_priv->irq_lock);
	}

	chv_set_pipe_power_well(dev_priv, power_well, false);

	if (power_well->data == PIPE_A)
		vlv_power_sequencer_reset(dev_priv);
}

static void check_power_well_state(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	bool enabled = power_well->ops->is_enabled(dev_priv, power_well);

	if (power_well->always_on || !i915.disable_power_well) {
		if (!enabled)
			goto mismatch;

		return;
	}

	if (enabled != (power_well->count > 0))
		goto mismatch;

	return;

mismatch:
	I915_STATE_WARN(1, "state mismatch for '%s' (always_on %d hw state %d use-count %d disable_power_well %d\n",
		  power_well->name, power_well->always_on, enabled,
		  power_well->count, i915.disable_power_well);
}

/**
 * intel_display_power_get - grab a power domain reference
 * @dev_priv: i915 device instance
 * @domain: power domain to reference
 *
 * This function grabs a power domain reference for @domain and ensures that the
 * power domain and all its parents are powered up. Therefore users should only
 * grab a reference to the innermost power domain they need.
 *
 * Any power domain reference obtained by this function must have a symmetric
 * call to intel_display_power_put() to release the reference again.
 */
void intel_display_power_get(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;
	int i;

	intel_runtime_pm_get(dev_priv);

	power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);

	for_each_power_well(i, power_well, BIT(domain), power_domains) {
		if (!power_well->count++) {
			DRM_DEBUG_KMS("enabling %s\n", power_well->name);
			power_well->ops->enable(dev_priv, power_well);
			power_well->hw_enabled = true;
		}

		check_power_well_state(dev_priv, power_well);
	}

	power_domains->domain_use_count[domain]++;

	mutex_unlock(&power_domains->lock);
}

/**
 * intel_display_power_put - release a power domain reference
 * @dev_priv: i915 device instance
 * @domain: power domain to reference
 *
 * This function drops the power domain reference obtained by
 * intel_display_power_get() and might power down the corresponding hardware
 * block right away if this is the last reference.
 */
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;
	int i;

	power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);

	WARN_ON(!power_domains->domain_use_count[domain]);
	power_domains->domain_use_count[domain]--;

	for_each_power_well_rev(i, power_well, BIT(domain), power_domains) {
		WARN_ON(!power_well->count);

		if (!--power_well->count && i915.disable_power_well) {
			DRM_DEBUG_KMS("disabling %s\n", power_well->name);
			power_well->hw_enabled = false;
			power_well->ops->disable(dev_priv, power_well);
		}

		check_power_well_state(dev_priv, power_well);
	}

	mutex_unlock(&power_domains->lock);

	intel_runtime_pm_put(dev_priv);
}

#define POWER_DOMAIN_MASK (BIT(POWER_DOMAIN_NUM) - 1)

#define HSW_ALWAYS_ON_POWER_DOMAINS (			\
	BIT(POWER_DOMAIN_PIPE_A) |			\
	BIT(POWER_DOMAIN_TRANSCODER_EDP) |		\
	BIT(POWER_DOMAIN_PORT_DDI_A_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_A_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_B_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_C_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_D_2_LANES) |		\
	BIT(POWER_DOMAIN_PORT_DDI_D_4_LANES) |		\
	BIT(POWER_DOMAIN_PORT_CRT) |			\
	BIT(POWER_DOMAIN_PLLS) |			\
	BIT(POWER_DOMAIN_INIT))
#define HSW_DISPLAY_POWER_DOMAINS (				\
	(POWER_DOMAIN_MASK & ~HSW_ALWAYS_ON_POWER_DOMAINS) |	\
	BIT(POWER_DOMAIN_INIT))

#define BDW_ALWAYS_ON_POWER_DOMAINS (			\
	HSW_ALWAYS_ON_POWER_DOMAINS |			\
	BIT(POWER_DOMAIN_PIPE_A_PANEL_FITTER))
#define BDW_DISPLAY_POWER_DOMAINS (				\
	(POWER_DOMAIN_MASK & ~BDW_ALWAYS_ON_POWER_DOMAINS) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_ALWAYS_ON_POWER_DOMAINS	BIT(POWER_DOMAIN_INIT)
#define VLV_DISPLAY_POWER_DOMAINS	POWER_DOMAIN_MASK

#define VLV_DPIO_CMN_BC_POWER_DOMAINS (		\
	BIT(POWER_DOMAIN_PORT_DDI_B_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |	\
	BIT(POWER_DOMAIN_PORT_CRT) |		\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_B_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_C_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define CHV_PIPE_A_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PIPE_A) |	\
	BIT(POWER_DOMAIN_INIT))

#define CHV_PIPE_B_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PIPE_B) |	\
	BIT(POWER_DOMAIN_INIT))

#define CHV_PIPE_C_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PIPE_C) |	\
	BIT(POWER_DOMAIN_INIT))

#define CHV_DPIO_CMN_BC_POWER_DOMAINS (		\
	BIT(POWER_DOMAIN_PORT_DDI_B_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_B_4_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_C_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define CHV_DPIO_CMN_D_POWER_DOMAINS (		\
	BIT(POWER_DOMAIN_PORT_DDI_D_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_D_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define CHV_DPIO_TX_D_LANES_01_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_D_2_LANES) |	\
	BIT(POWER_DOMAIN_PORT_DDI_D_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

#define CHV_DPIO_TX_D_LANES_23_POWER_DOMAINS (	\
	BIT(POWER_DOMAIN_PORT_DDI_D_4_LANES) |	\
	BIT(POWER_DOMAIN_INIT))

static const struct i915_power_well_ops i9xx_always_on_power_well_ops = {
	.sync_hw = i9xx_always_on_power_well_noop,
	.enable = i9xx_always_on_power_well_noop,
	.disable = i9xx_always_on_power_well_noop,
	.is_enabled = i9xx_always_on_power_well_enabled,
};

static const struct i915_power_well_ops chv_pipe_power_well_ops = {
	.sync_hw = chv_pipe_power_well_sync_hw,
	.enable = chv_pipe_power_well_enable,
	.disable = chv_pipe_power_well_disable,
	.is_enabled = chv_pipe_power_well_enabled,
};

static const struct i915_power_well_ops chv_dpio_cmn_power_well_ops = {
	.sync_hw = vlv_power_well_sync_hw,
	.enable = chv_dpio_cmn_power_well_enable,
	.disable = chv_dpio_cmn_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static struct i915_power_well i9xx_always_on_power_well[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
	},
};

static const struct i915_power_well_ops hsw_power_well_ops = {
	.sync_hw = hsw_power_well_sync_hw,
	.enable = hsw_power_well_enable,
	.disable = hsw_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

static struct i915_power_well hsw_power_wells[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = HSW_ALWAYS_ON_POWER_DOMAINS,
		.ops = &i9xx_always_on_power_well_ops,
	},
	{
		.name = "display",
		.domains = HSW_DISPLAY_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
	},
};

static struct i915_power_well bdw_power_wells[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = BDW_ALWAYS_ON_POWER_DOMAINS,
		.ops = &i9xx_always_on_power_well_ops,
	},
	{
		.name = "display",
		.domains = BDW_DISPLAY_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
	},
};

static const struct i915_power_well_ops vlv_display_power_well_ops = {
	.sync_hw = vlv_power_well_sync_hw,
	.enable = vlv_display_power_well_enable,
	.disable = vlv_display_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_ops vlv_dpio_cmn_power_well_ops = {
	.sync_hw = vlv_power_well_sync_hw,
	.enable = vlv_dpio_cmn_power_well_enable,
	.disable = vlv_dpio_cmn_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_ops vlv_dpio_power_well_ops = {
	.sync_hw = vlv_power_well_sync_hw,
	.enable = vlv_power_well_enable,
	.disable = vlv_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static struct i915_power_well vlv_power_wells[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = VLV_ALWAYS_ON_POWER_DOMAINS,
		.ops = &i9xx_always_on_power_well_ops,
	},
	{
		.name = "display",
		.domains = VLV_DISPLAY_POWER_DOMAINS,
		.data = PUNIT_POWER_WELL_DISP2D,
		.ops = &vlv_display_power_well_ops,
	},
	{
		.name = "dpio-tx-b-01",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_B_LANES_01,
	},
	{
		.name = "dpio-tx-b-23",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_B_LANES_23,
	},
	{
		.name = "dpio-tx-c-01",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_C_LANES_01,
	},
	{
		.name = "dpio-tx-c-23",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_C_LANES_23,
	},
	{
		.name = "dpio-common",
		.domains = VLV_DPIO_CMN_BC_POWER_DOMAINS,
		.data = PUNIT_POWER_WELL_DPIO_CMN_BC,
		.ops = &vlv_dpio_cmn_power_well_ops,
	},
};

static struct i915_power_well chv_power_wells[] = {
	{
		.name = "always-on",
		.always_on = 1,
		.domains = VLV_ALWAYS_ON_POWER_DOMAINS,
		.ops = &i9xx_always_on_power_well_ops,
	},
#if 0
	{
		.name = "display",
		.domains = VLV_DISPLAY_POWER_DOMAINS,
		.data = PUNIT_POWER_WELL_DISP2D,
		.ops = &vlv_display_power_well_ops,
	},
#endif
	{
		.name = "pipe-a",
		/*
		 * FIXME: pipe A power well seems to be the new disp2d well.
		 * At least all registers seem to be housed there. Figure
		 * out if this a a temporary situation in pre-production
		 * hardware or a permanent state of affairs.
		 */
		.domains = CHV_PIPE_A_POWER_DOMAINS | VLV_DISPLAY_POWER_DOMAINS,
		.data = PIPE_A,
		.ops = &chv_pipe_power_well_ops,
	},
#if 0
	{
		.name = "pipe-b",
		.domains = CHV_PIPE_B_POWER_DOMAINS,
		.data = PIPE_B,
		.ops = &chv_pipe_power_well_ops,
	},
	{
		.name = "pipe-c",
		.domains = CHV_PIPE_C_POWER_DOMAINS,
		.data = PIPE_C,
		.ops = &chv_pipe_power_well_ops,
	},
#endif
	{
		.name = "dpio-common-bc",
		/*
		 * XXX: cmnreset for one PHY seems to disturb the other.
		 * As a workaround keep both powered on at the same
		 * time for now.
		 */
		.domains = CHV_DPIO_CMN_BC_POWER_DOMAINS | CHV_DPIO_CMN_D_POWER_DOMAINS,
		.data = PUNIT_POWER_WELL_DPIO_CMN_BC,
		.ops = &chv_dpio_cmn_power_well_ops,
	},
	{
		.name = "dpio-common-d",
		/*
		 * XXX: cmnreset for one PHY seems to disturb the other.
		 * As a workaround keep both powered on at the same
		 * time for now.
		 */
		.domains = CHV_DPIO_CMN_BC_POWER_DOMAINS | CHV_DPIO_CMN_D_POWER_DOMAINS,
		.data = PUNIT_POWER_WELL_DPIO_CMN_D,
		.ops = &chv_dpio_cmn_power_well_ops,
	},
#if 0
	{
		.name = "dpio-tx-b-01",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_B_LANES_01,
	},
	{
		.name = "dpio-tx-b-23",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_B_LANES_23,
	},
	{
		.name = "dpio-tx-c-01",
		.domains = VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_C_LANES_01,
	},
	{
		.name = "dpio-tx-c-23",
		.domains = VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_C_LANES_23,
	},
	{
		.name = "dpio-tx-d-01",
		.domains = CHV_DPIO_TX_D_LANES_01_POWER_DOMAINS |
			   CHV_DPIO_TX_D_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_D_LANES_01,
	},
	{
		.name = "dpio-tx-d-23",
		.domains = CHV_DPIO_TX_D_LANES_01_POWER_DOMAINS |
			   CHV_DPIO_TX_D_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.data = PUNIT_POWER_WELL_DPIO_TX_D_LANES_23,
	},
#endif
};

static struct i915_power_well *lookup_power_well(struct drm_i915_private *dev_priv,
						 enum punit_power_well power_well_id)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;
	int i;

	for_each_power_well(i, power_well, POWER_DOMAIN_MASK, power_domains) {
		if (power_well->data == power_well_id)
			return power_well;
	}

	return NULL;
}

#define set_power_wells(power_domains, __power_wells) ({		\
	(power_domains)->power_wells = (__power_wells);			\
	(power_domains)->power_well_count = ARRAY_SIZE(__power_wells);	\
})

/**
 * intel_power_domains_init - initializes the power domain structures
 * @dev_priv: i915 device instance
 *
 * Initializes the power domain structures for @dev_priv depending upon the
 * supported platform.
 */
int intel_power_domains_init(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;

	mutex_init(&power_domains->lock);

	/*
	 * The enabling order will be from lower to higher indexed wells,
	 * the disabling order is reversed.
	 */
	if (IS_HASWELL(dev_priv->dev)) {
		set_power_wells(power_domains, hsw_power_wells);
		hsw_pwr = power_domains;
	} else if (IS_BROADWELL(dev_priv->dev)) {
		set_power_wells(power_domains, bdw_power_wells);
		hsw_pwr = power_domains;
	} else if (IS_CHERRYVIEW(dev_priv->dev)) {
		set_power_wells(power_domains, chv_power_wells);
	} else if (IS_VALLEYVIEW(dev_priv->dev)) {
		set_power_wells(power_domains, vlv_power_wells);
	} else {
		set_power_wells(power_domains, i9xx_always_on_power_well);
	}

	return 0;
}

static void intel_runtime_pm_disable(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	if (!intel_enable_rc6(dev))
		return;

	/* Make sure we're not suspended first. */
	pm_runtime_get_sync(device);
	pm_runtime_disable(device);
}

/**
 * intel_power_domains_fini - finalizes the power domain structures
 * @dev_priv: i915 device instance
 *
 * Finalizes the power domain structures for @dev_priv depending upon the
 * supported platform. This function also disables runtime pm and ensures that
 * the device stays powered up so that the driver can be reloaded.
 */
void intel_power_domains_fini(struct drm_i915_private *dev_priv)
{
	intel_runtime_pm_disable(dev_priv);

	/* The i915.ko module is still not prepared to be loaded when
	 * the power well is not enabled, so just enable it in case
	 * we're going to unload/reload. */
	intel_display_set_init_power(dev_priv, true);

	hsw_pwr = NULL;
}

static void intel_power_domains_resume(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;
	int i;

	mutex_lock(&power_domains->lock);
	for_each_power_well(i, power_well, POWER_DOMAIN_MASK, power_domains) {
		power_well->ops->sync_hw(dev_priv, power_well);
		power_well->hw_enabled = power_well->ops->is_enabled(dev_priv,
								     power_well);
	}
	mutex_unlock(&power_domains->lock);
}

static void vlv_cmnlane_wa(struct drm_i915_private *dev_priv)
{
	struct i915_power_well *cmn =
		lookup_power_well(dev_priv, PUNIT_POWER_WELL_DPIO_CMN_BC);
	struct i915_power_well *disp2d =
		lookup_power_well(dev_priv, PUNIT_POWER_WELL_DISP2D);

	/* If the display might be already active skip this */
	if (cmn->ops->is_enabled(dev_priv, cmn) &&
	    disp2d->ops->is_enabled(dev_priv, disp2d) &&
	    I915_READ(DPIO_CTL) & DPIO_CMNRST)
		return;

	DRM_DEBUG_KMS("toggling display PHY side reset\n");

	/* cmnlane needs DPLL registers */
	disp2d->ops->enable(dev_priv, disp2d);

	/*
	 * From VLV2A0_DP_eDP_HDMI_DPIO_driver_vbios_notes_11.docx:
	 * Need to assert and de-assert PHY SB reset by gating the
	 * common lane power, then un-gating it.
	 * Simply ungating isn't enough to reset the PHY enough to get
	 * ports and lanes running.
	 */
	cmn->ops->disable(dev_priv, cmn);
}

/**
 * intel_power_domains_init_hw - initialize hardware power domain state
 * @dev_priv: i915 device instance
 *
 * This function initializes the hardware power domain state and enables all
 * power domains using intel_display_set_init_power().
 */
void intel_power_domains_init_hw(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct i915_power_domains *power_domains = &dev_priv->power_domains;

	power_domains->initializing = true;

	if (IS_VALLEYVIEW(dev) && !IS_CHERRYVIEW(dev)) {
		mutex_lock(&power_domains->lock);
		vlv_cmnlane_wa(dev_priv);
		mutex_unlock(&power_domains->lock);
	}

	/* For now, we need the power well to be always enabled. */
	intel_display_set_init_power(dev_priv, true);
	intel_power_domains_resume(dev_priv);
	power_domains->initializing = false;
}

/**
 * intel_aux_display_runtime_get - grab an auxilliary power domain reference
 * @dev_priv: i915 device instance
 *
 * This function grabs a power domain reference for the auxiliary power domain
 * (for access to the GMBUS and DP AUX blocks) and ensures that it and all its
 * parents are powered up. Therefore users should only grab a reference to the
 * innermost power domain they need.
 *
 * Any power domain reference obtained by this function must have a symmetric
 * call to intel_aux_display_runtime_put() to release the reference again.
 */
void intel_aux_display_runtime_get(struct drm_i915_private *dev_priv)
{
	intel_runtime_pm_get(dev_priv);
}

/**
 * intel_aux_display_runtime_put - release an auxilliary power domain reference
 * @dev_priv: i915 device instance
 *
 * This function drops the auxilliary power domain reference obtained by
 * intel_aux_display_runtime_get() and might power down the corresponding
 * hardware block right away if this is the last reference.
 */
void intel_aux_display_runtime_put(struct drm_i915_private *dev_priv)
{
	intel_runtime_pm_put(dev_priv);
}

/**
 * intel_runtime_pm_get - grab a runtime pm reference
 * @dev_priv: i915 device instance
 *
 * This function grabs a device-level runtime pm reference (mostly used for GEM
 * code to ensure the GTT or GT is on) and ensures that it is powered up.
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put() to release the reference again.
 */
void intel_runtime_pm_get(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	pm_runtime_get_sync(device);
	WARN(dev_priv->pm.suspended, "Device still suspended.\n");
}

/**
 * intel_runtime_pm_get_noresume - grab a runtime pm reference
 * @dev_priv: i915 device instance
 *
 * This function grabs a device-level runtime pm reference (mostly used for GEM
 * code to ensure the GTT or GT is on).
 *
 * It will _not_ power up the device but instead only check that it's powered
 * on.  Therefore it is only valid to call this functions from contexts where
 * the device is known to be powered up and where trying to power it up would
 * result in hilarity and deadlocks. That pretty much means only the system
 * suspend/resume code where this is used to grab runtime pm references for
 * delayed setup down in work items.
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put() to release the reference again.
 */
void intel_runtime_pm_get_noresume(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	WARN(dev_priv->pm.suspended, "Getting nosync-ref while suspended.\n");
	pm_runtime_get_noresume(device);
}

/**
 * intel_runtime_pm_put - release a runtime pm reference
 * @dev_priv: i915 device instance
 *
 * This function drops the device-level runtime pm reference obtained by
 * intel_runtime_pm_get() and might power down the corresponding
 * hardware block right away if this is the last reference.
 */
void intel_runtime_pm_put(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	pm_runtime_mark_last_busy(device);
	pm_runtime_put_autosuspend(device);
}

/**
 * intel_runtime_pm_enable - enable runtime pm
 * @dev_priv: i915 device instance
 *
 * This function enables runtime pm at the end of the driver load sequence.
 *
 * Note that this function does currently not enable runtime pm for the
 * subordinate display power domains. That is only done on the first modeset
 * using intel_display_set_init_power().
 */
void intel_runtime_pm_enable(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	struct device *device = &dev->pdev->dev;

	if (!HAS_RUNTIME_PM(dev))
		return;

	pm_runtime_set_active(device);

	/*
	 * RPM depends on RC6 to save restore the GT HW context, so make RC6 a
	 * requirement.
	 */
	if (!intel_enable_rc6(dev)) {
		DRM_INFO("RC6 disabled, disabling runtime PM support\n");
		return;
	}

	pm_runtime_set_autosuspend_delay(device, 10000); /* 10s */
	pm_runtime_mark_last_busy(device);
	pm_runtime_use_autosuspend(device);

	pm_runtime_put_autosuspend(device);
}

/* Display audio driver power well request */
int i915_request_power_well(void)
{
	struct drm_i915_private *dev_priv;

	if (!hsw_pwr)
		return -ENODEV;

	dev_priv = container_of(hsw_pwr, struct drm_i915_private,
				power_domains);
	intel_display_power_get(dev_priv, POWER_DOMAIN_AUDIO);
	return 0;
}
EXPORT_SYMBOL_GPL(i915_request_power_well);

/* Display audio driver power well release */
int i915_release_power_well(void)
{
	struct drm_i915_private *dev_priv;

	if (!hsw_pwr)
		return -ENODEV;

	dev_priv = container_of(hsw_pwr, struct drm_i915_private,
				power_domains);
	intel_display_power_put(dev_priv, POWER_DOMAIN_AUDIO);
	return 0;
}
EXPORT_SYMBOL_GPL(i915_release_power_well);

/*
 * Private interface for the audio driver to get CDCLK in kHz.
 *
 * Caller must request power well using i915_request_power_well() prior to
 * making the call.
 */
int i915_get_cdclk_freq(void)
{
	struct drm_i915_private *dev_priv;

	if (!hsw_pwr)
		return -ENODEV;

	dev_priv = container_of(hsw_pwr, struct drm_i915_private,
				power_domains);

	return intel_ddi_get_cdclk_freq(dev_priv);
}
EXPORT_SYMBOL_GPL(i915_get_cdclk_freq);
