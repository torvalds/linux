// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_types.h"
#include "intel_dp_aux.h"
#include "intel_gmbus.h"
#include "intel_hotplug.h"
#include "intel_hotplug_irq.h"

typedef bool (*long_pulse_detect_func)(enum hpd_pin pin, u32 val);
typedef u32 (*hotplug_enables_func)(struct intel_encoder *encoder);
typedef u32 (*hotplug_mask_func)(enum hpd_pin pin);

static const u32 hpd_ilk[HPD_NUM_PINS] = {
	[HPD_PORT_A] = DE_DP_A_HOTPLUG,
};

static const u32 hpd_ivb[HPD_NUM_PINS] = {
	[HPD_PORT_A] = DE_DP_A_HOTPLUG_IVB,
};

static const u32 hpd_bdw[HPD_NUM_PINS] = {
	[HPD_PORT_A] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_A),
};

static const u32 hpd_ibx[HPD_NUM_PINS] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG,
};

static const u32 hpd_cpt[HPD_NUM_PINS] = {
	[HPD_CRT] = SDE_CRT_HOTPLUG_CPT,
	[HPD_SDVO_B] = SDE_SDVOB_HOTPLUG_CPT,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG_CPT,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG_CPT,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG_CPT,
};

static const u32 hpd_spt[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_PORTA_HOTPLUG_SPT,
	[HPD_PORT_B] = SDE_PORTB_HOTPLUG_CPT,
	[HPD_PORT_C] = SDE_PORTC_HOTPLUG_CPT,
	[HPD_PORT_D] = SDE_PORTD_HOTPLUG_CPT,
	[HPD_PORT_E] = SDE_PORTE_HOTPLUG_SPT,
};

static const u32 hpd_mask_i915[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_EN,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_EN,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_EN,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_EN,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_EN,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_EN,
};

static const u32 hpd_status_g4x[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_G4X,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_G4X,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS,
};

static const u32 hpd_status_i915[HPD_NUM_PINS] = {
	[HPD_CRT] = CRT_HOTPLUG_INT_STATUS,
	[HPD_SDVO_B] = SDVOB_HOTPLUG_INT_STATUS_I915,
	[HPD_SDVO_C] = SDVOC_HOTPLUG_INT_STATUS_I915,
	[HPD_PORT_B] = PORTB_HOTPLUG_INT_STATUS,
	[HPD_PORT_C] = PORTC_HOTPLUG_INT_STATUS,
	[HPD_PORT_D] = PORTD_HOTPLUG_INT_STATUS,
};

static const u32 hpd_bxt[HPD_NUM_PINS] = {
	[HPD_PORT_A] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_A),
	[HPD_PORT_B] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_B),
	[HPD_PORT_C] = GEN8_DE_PORT_HOTPLUG(HPD_PORT_C),
};

static const u32 hpd_gen11[HPD_NUM_PINS] = {
	[HPD_PORT_TC1] = GEN11_TC_HOTPLUG(HPD_PORT_TC1) | GEN11_TBT_HOTPLUG(HPD_PORT_TC1),
	[HPD_PORT_TC2] = GEN11_TC_HOTPLUG(HPD_PORT_TC2) | GEN11_TBT_HOTPLUG(HPD_PORT_TC2),
	[HPD_PORT_TC3] = GEN11_TC_HOTPLUG(HPD_PORT_TC3) | GEN11_TBT_HOTPLUG(HPD_PORT_TC3),
	[HPD_PORT_TC4] = GEN11_TC_HOTPLUG(HPD_PORT_TC4) | GEN11_TBT_HOTPLUG(HPD_PORT_TC4),
	[HPD_PORT_TC5] = GEN11_TC_HOTPLUG(HPD_PORT_TC5) | GEN11_TBT_HOTPLUG(HPD_PORT_TC5),
	[HPD_PORT_TC6] = GEN11_TC_HOTPLUG(HPD_PORT_TC6) | GEN11_TBT_HOTPLUG(HPD_PORT_TC6),
};

static const u32 hpd_xelpdp[HPD_NUM_PINS] = {
	[HPD_PORT_TC1] = XELPDP_TBT_HOTPLUG(HPD_PORT_TC1) | XELPDP_DP_ALT_HOTPLUG(HPD_PORT_TC1),
	[HPD_PORT_TC2] = XELPDP_TBT_HOTPLUG(HPD_PORT_TC2) | XELPDP_DP_ALT_HOTPLUG(HPD_PORT_TC2),
	[HPD_PORT_TC3] = XELPDP_TBT_HOTPLUG(HPD_PORT_TC3) | XELPDP_DP_ALT_HOTPLUG(HPD_PORT_TC3),
	[HPD_PORT_TC4] = XELPDP_TBT_HOTPLUG(HPD_PORT_TC4) | XELPDP_DP_ALT_HOTPLUG(HPD_PORT_TC4),
};

static const u32 hpd_icp[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_A),
	[HPD_PORT_B] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_B),
	[HPD_PORT_C] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_C),
	[HPD_PORT_TC1] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC1),
	[HPD_PORT_TC2] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC2),
	[HPD_PORT_TC3] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC3),
	[HPD_PORT_TC4] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC4),
	[HPD_PORT_TC5] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC5),
	[HPD_PORT_TC6] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC6),
};

static const u32 hpd_sde_dg1[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_A),
	[HPD_PORT_B] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_B),
	[HPD_PORT_C] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_C),
	[HPD_PORT_D] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_D),
	[HPD_PORT_TC1] = SDE_TC_HOTPLUG_DG2(HPD_PORT_TC1),
};

static const u32 hpd_mtp[HPD_NUM_PINS] = {
	[HPD_PORT_A] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_A),
	[HPD_PORT_B] = SDE_DDI_HOTPLUG_ICP(HPD_PORT_B),
	[HPD_PORT_TC1] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC1),
	[HPD_PORT_TC2] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC2),
	[HPD_PORT_TC3] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC3),
	[HPD_PORT_TC4] = SDE_TC_HOTPLUG_ICP(HPD_PORT_TC4),
};

static void intel_hpd_init_pins(struct drm_i915_private *dev_priv)
{
	struct intel_hotplug *hpd = &dev_priv->display.hotplug;

	if (HAS_GMCH(dev_priv)) {
		if (IS_G4X(dev_priv) || IS_VALLEYVIEW(dev_priv) ||
		    IS_CHERRYVIEW(dev_priv))
			hpd->hpd = hpd_status_g4x;
		else
			hpd->hpd = hpd_status_i915;
		return;
	}

	if (DISPLAY_VER(dev_priv) >= 14)
		hpd->hpd = hpd_xelpdp;
	else if (DISPLAY_VER(dev_priv) >= 11)
		hpd->hpd = hpd_gen11;
	else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		hpd->hpd = hpd_bxt;
	else if (DISPLAY_VER(dev_priv) == 9)
		hpd->hpd = NULL; /* no north HPD on SKL */
	else if (DISPLAY_VER(dev_priv) >= 8)
		hpd->hpd = hpd_bdw;
	else if (DISPLAY_VER(dev_priv) >= 7)
		hpd->hpd = hpd_ivb;
	else
		hpd->hpd = hpd_ilk;

	if ((INTEL_PCH_TYPE(dev_priv) < PCH_DG1) &&
	    (!HAS_PCH_SPLIT(dev_priv) || HAS_PCH_NOP(dev_priv)))
		return;

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_MTL)
		hpd->pch_hpd = hpd_mtp;
	else if (INTEL_PCH_TYPE(dev_priv) >= PCH_DG1)
		hpd->pch_hpd = hpd_sde_dg1;
	else if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		hpd->pch_hpd = hpd_icp;
	else if (HAS_PCH_CNP(dev_priv) || HAS_PCH_SPT(dev_priv))
		hpd->pch_hpd = hpd_spt;
	else if (HAS_PCH_LPT(dev_priv) || HAS_PCH_CPT(dev_priv))
		hpd->pch_hpd = hpd_cpt;
	else if (HAS_PCH_IBX(dev_priv))
		hpd->pch_hpd = hpd_ibx;
	else
		MISSING_CASE(INTEL_PCH_TYPE(dev_priv));
}

/* For display hotplug interrupt */
void i915_hotplug_interrupt_update_locked(struct drm_i915_private *dev_priv,
					  u32 mask, u32 bits)
{
	lockdep_assert_held(&dev_priv->irq_lock);
	drm_WARN_ON(&dev_priv->drm, bits & ~mask);

	intel_uncore_rmw(&dev_priv->uncore, PORT_HOTPLUG_EN(dev_priv), mask,
			 bits);
}

/**
 * i915_hotplug_interrupt_update - update hotplug interrupt enable
 * @dev_priv: driver private
 * @mask: bits to update
 * @bits: bits to enable
 * NOTE: the HPD enable bits are modified both inside and outside
 * of an interrupt context. To avoid that read-modify-write cycles
 * interfer, these bits are protected by a spinlock. Since this
 * function is usually not called from a context where the lock is
 * held already, this function acquires the lock itself. A non-locking
 * version is also available.
 */
void i915_hotplug_interrupt_update(struct drm_i915_private *dev_priv,
				   u32 mask,
				   u32 bits)
{
	spin_lock_irq(&dev_priv->irq_lock);
	i915_hotplug_interrupt_update_locked(dev_priv, mask, bits);
	spin_unlock_irq(&dev_priv->irq_lock);
}

static bool gen11_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return val & GEN11_HOTPLUG_CTL_LONG_DETECT(pin);
	default:
		return false;
	}
}

static bool bxt_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
		return val & PORTA_HOTPLUG_LONG_DETECT;
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool icp_ddi_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
	case HPD_PORT_B:
	case HPD_PORT_C:
	case HPD_PORT_D:
		return val & SHOTPLUG_CTL_DDI_HPD_LONG_DETECT(pin);
	default:
		return false;
	}
}

static bool icp_tc_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return val & ICP_TC_HPD_LONG_DETECT(pin);
	default:
		return false;
	}
}

static bool spt_port_hotplug2_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_E:
		return val & PORTE_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool spt_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
		return val & PORTA_HOTPLUG_LONG_DETECT;
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	case HPD_PORT_D:
		return val & PORTD_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool ilk_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_A:
		return val & DIGITAL_PORTA_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool pch_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_LONG_DETECT;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_LONG_DETECT;
	case HPD_PORT_D:
		return val & PORTD_HOTPLUG_LONG_DETECT;
	default:
		return false;
	}
}

static bool i9xx_port_hotplug_long_detect(enum hpd_pin pin, u32 val)
{
	switch (pin) {
	case HPD_PORT_B:
		return val & PORTB_HOTPLUG_INT_LONG_PULSE;
	case HPD_PORT_C:
		return val & PORTC_HOTPLUG_INT_LONG_PULSE;
	case HPD_PORT_D:
		return val & PORTD_HOTPLUG_INT_LONG_PULSE;
	default:
		return false;
	}
}

/*
 * Get a bit mask of pins that have triggered, and which ones may be long.
 * This can be called multiple times with the same masks to accumulate
 * hotplug detection results from several registers.
 *
 * Note that the caller is expected to zero out the masks initially.
 */
static void intel_get_hpd_pins(struct drm_i915_private *dev_priv,
			       u32 *pin_mask, u32 *long_mask,
			       u32 hotplug_trigger, u32 dig_hotplug_reg,
			       const u32 hpd[HPD_NUM_PINS],
			       bool long_pulse_detect(enum hpd_pin pin, u32 val))
{
	enum hpd_pin pin;

	BUILD_BUG_ON(BITS_PER_TYPE(*pin_mask) < HPD_NUM_PINS);

	for_each_hpd_pin(pin) {
		if ((hpd[pin] & hotplug_trigger) == 0)
			continue;

		*pin_mask |= BIT(pin);

		if (long_pulse_detect(pin, dig_hotplug_reg))
			*long_mask |= BIT(pin);
	}

	drm_dbg(&dev_priv->drm,
		"hotplug event received, stat 0x%08x, dig 0x%08x, pins 0x%08x, long 0x%08x\n",
		hotplug_trigger, dig_hotplug_reg, *pin_mask, *long_mask);
}

static u32 intel_hpd_enabled_irqs(struct drm_i915_private *dev_priv,
				  const u32 hpd[HPD_NUM_PINS])
{
	struct intel_encoder *encoder;
	u32 enabled_irqs = 0;

	for_each_intel_encoder(&dev_priv->drm, encoder)
		if (dev_priv->display.hotplug.stats[encoder->hpd_pin].state == HPD_ENABLED)
			enabled_irqs |= hpd[encoder->hpd_pin];

	return enabled_irqs;
}

static u32 intel_hpd_hotplug_irqs(struct drm_i915_private *dev_priv,
				  const u32 hpd[HPD_NUM_PINS])
{
	struct intel_encoder *encoder;
	u32 hotplug_irqs = 0;

	for_each_intel_encoder(&dev_priv->drm, encoder)
		hotplug_irqs |= hpd[encoder->hpd_pin];

	return hotplug_irqs;
}

static u32 intel_hpd_hotplug_mask(struct drm_i915_private *i915,
				  hotplug_mask_func hotplug_mask)
{
	enum hpd_pin pin;
	u32 hotplug = 0;

	for_each_hpd_pin(pin)
		hotplug |= hotplug_mask(pin);

	return hotplug;
}

static u32 intel_hpd_hotplug_enables(struct drm_i915_private *i915,
				     hotplug_enables_func hotplug_enables)
{
	struct intel_encoder *encoder;
	u32 hotplug = 0;

	for_each_intel_encoder(&i915->drm, encoder)
		hotplug |= hotplug_enables(encoder);

	return hotplug;
}

u32 i9xx_hpd_irq_ack(struct drm_i915_private *dev_priv)
{
	u32 hotplug_status = 0, hotplug_status_mask;
	int i;

	if (IS_G4X(dev_priv) ||
	    IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		hotplug_status_mask = HOTPLUG_INT_STATUS_G4X |
			DP_AUX_CHANNEL_MASK_INT_STATUS_G4X;
	else
		hotplug_status_mask = HOTPLUG_INT_STATUS_I915;

	/*
	 * We absolutely have to clear all the pending interrupt
	 * bits in PORT_HOTPLUG_STAT. Otherwise the ISR port
	 * interrupt bit won't have an edge, and the i965/g4x
	 * edge triggered IIR will not notice that an interrupt
	 * is still pending. We can't use PORT_HOTPLUG_EN to
	 * guarantee the edge as the act of toggling the enable
	 * bits can itself generate a new hotplug interrupt :(
	 */
	for (i = 0; i < 10; i++) {
		u32 tmp = intel_uncore_read(&dev_priv->uncore,
					    PORT_HOTPLUG_STAT(dev_priv)) & hotplug_status_mask;

		if (tmp == 0)
			return hotplug_status;

		hotplug_status |= tmp;
		intel_uncore_write(&dev_priv->uncore,
				   PORT_HOTPLUG_STAT(dev_priv),
				   hotplug_status);
	}

	drm_WARN_ONCE(&dev_priv->drm, 1,
		      "PORT_HOTPLUG_STAT did not clear (0x%08x)\n",
		      intel_uncore_read(&dev_priv->uncore, PORT_HOTPLUG_STAT(dev_priv)));

	return hotplug_status;
}

void i9xx_hpd_irq_handler(struct drm_i915_private *dev_priv, u32 hotplug_status)
{
	struct intel_display *display = &dev_priv->display;
	u32 pin_mask = 0, long_mask = 0;
	u32 hotplug_trigger;

	if (IS_G4X(dev_priv) ||
	    IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_G4X;
	else
		hotplug_trigger = hotplug_status & HOTPLUG_INT_STATUS_I915;

	if (hotplug_trigger) {
		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug_trigger, hotplug_trigger,
				   dev_priv->display.hotplug.hpd,
				   i9xx_port_hotplug_long_detect);

		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
	}

	if ((IS_G4X(dev_priv) ||
	     IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    hotplug_status & DP_AUX_CHANNEL_MASK_INT_STATUS_G4X)
		intel_dp_aux_irq_handler(display);
}

void ibx_hpd_irq_handler(struct drm_i915_private *dev_priv, u32 hotplug_trigger)
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	/*
	 * Somehow the PCH doesn't seem to really ack the interrupt to the CPU
	 * unless we touch the hotplug register, even if hotplug_trigger is
	 * zero. Not acking leads to "The master control interrupt lied (SDE)!"
	 * errors.
	 */
	dig_hotplug_reg = intel_uncore_read(&dev_priv->uncore, PCH_PORT_HOTPLUG);
	if (!hotplug_trigger) {
		u32 mask = PORTA_HOTPLUG_STATUS_MASK |
			PORTD_HOTPLUG_STATUS_MASK |
			PORTC_HOTPLUG_STATUS_MASK |
			PORTB_HOTPLUG_STATUS_MASK;
		dig_hotplug_reg &= ~mask;
	}

	intel_uncore_write(&dev_priv->uncore, PCH_PORT_HOTPLUG, dig_hotplug_reg);
	if (!hotplug_trigger)
		return;

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
			   hotplug_trigger, dig_hotplug_reg,
			   dev_priv->display.hotplug.pch_hpd,
			   pch_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

void xelpdp_pica_irq_handler(struct drm_i915_private *i915, u32 iir)
{
	struct intel_display *display = &i915->display;
	enum hpd_pin pin;
	u32 hotplug_trigger = iir & (XELPDP_DP_ALT_HOTPLUG_MASK | XELPDP_TBT_HOTPLUG_MASK);
	u32 trigger_aux = iir & XELPDP_AUX_TC_MASK;
	u32 pin_mask = 0, long_mask = 0;

	if (DISPLAY_VER(i915) >= 20)
		trigger_aux |= iir & XE2LPD_AUX_DDI_MASK;

	for (pin = HPD_PORT_TC1; pin <= HPD_PORT_TC4; pin++) {
		u32 val;

		if (!(i915->display.hotplug.hpd[pin] & hotplug_trigger))
			continue;

		pin_mask |= BIT(pin);

		val = intel_de_read(i915, XELPDP_PORT_HOTPLUG_CTL(pin));
		intel_de_write(i915, XELPDP_PORT_HOTPLUG_CTL(pin), val);

		if (val & (XELPDP_DP_ALT_HPD_LONG_DETECT | XELPDP_TBT_HPD_LONG_DETECT))
			long_mask |= BIT(pin);
	}

	if (pin_mask) {
		drm_dbg(&i915->drm,
			"pica hotplug event received, stat 0x%08x, pins 0x%08x, long 0x%08x\n",
			hotplug_trigger, pin_mask, long_mask);

		intel_hpd_irq_handler(i915, pin_mask, long_mask);
	}

	if (trigger_aux)
		intel_dp_aux_irq_handler(display);

	if (!pin_mask && !trigger_aux)
		drm_err(&i915->drm,
			"Unexpected DE HPD/AUX interrupt 0x%08x\n", iir);
}

void icp_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	u32 ddi_hotplug_trigger = pch_iir & SDE_DDI_HOTPLUG_MASK_ICP;
	u32 tc_hotplug_trigger = pch_iir & SDE_TC_HOTPLUG_MASK_ICP;
	u32 pin_mask = 0, long_mask = 0;

	if (ddi_hotplug_trigger) {
		u32 dig_hotplug_reg;

		/* Locking due to DSI native GPIO sequences */
		spin_lock(&dev_priv->irq_lock);
		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, SHOTPLUG_CTL_DDI, 0, 0);
		spin_unlock(&dev_priv->irq_lock);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   ddi_hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   icp_ddi_port_hotplug_long_detect);
	}

	if (tc_hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, SHOTPLUG_CTL_TC, 0, 0);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   tc_hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   icp_tc_port_hotplug_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);

	if (pch_iir & SDE_GMBUS_ICP)
		intel_gmbus_irq_handler(dev_priv);
}

void spt_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir)
{
	u32 hotplug_trigger = pch_iir & SDE_HOTPLUG_MASK_SPT &
		~SDE_PORTE_HOTPLUG_SPT;
	u32 hotplug2_trigger = pch_iir & SDE_PORTE_HOTPLUG_SPT;
	u32 pin_mask = 0, long_mask = 0;

	if (hotplug_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, PCH_PORT_HOTPLUG, 0, 0);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   spt_port_hotplug_long_detect);
	}

	if (hotplug2_trigger) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, PCH_PORT_HOTPLUG2, 0, 0);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   hotplug2_trigger, dig_hotplug_reg,
				   dev_priv->display.hotplug.pch_hpd,
				   spt_port_hotplug2_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);

	if (pch_iir & SDE_GMBUS_CPT)
		intel_gmbus_irq_handler(dev_priv);
}

void ilk_hpd_irq_handler(struct drm_i915_private *dev_priv, u32 hotplug_trigger)
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, DIGITAL_PORT_HOTPLUG_CNTRL, 0, 0);

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
			   hotplug_trigger, dig_hotplug_reg,
			   dev_priv->display.hotplug.hpd,
			   ilk_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

void bxt_hpd_irq_handler(struct drm_i915_private *dev_priv, u32 hotplug_trigger)
{
	u32 dig_hotplug_reg, pin_mask = 0, long_mask = 0;

	dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, PCH_PORT_HOTPLUG, 0, 0);

	intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
			   hotplug_trigger, dig_hotplug_reg,
			   dev_priv->display.hotplug.hpd,
			   bxt_port_hotplug_long_detect);

	intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
}

void gen11_hpd_irq_handler(struct drm_i915_private *dev_priv, u32 iir)
{
	u32 pin_mask = 0, long_mask = 0;
	u32 trigger_tc = iir & GEN11_DE_TC_HOTPLUG_MASK;
	u32 trigger_tbt = iir & GEN11_DE_TBT_HOTPLUG_MASK;

	if (trigger_tc) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, GEN11_TC_HOTPLUG_CTL, 0, 0);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   trigger_tc, dig_hotplug_reg,
				   dev_priv->display.hotplug.hpd,
				   gen11_port_hotplug_long_detect);
	}

	if (trigger_tbt) {
		u32 dig_hotplug_reg;

		dig_hotplug_reg = intel_uncore_rmw(&dev_priv->uncore, GEN11_TBT_HOTPLUG_CTL, 0, 0);

		intel_get_hpd_pins(dev_priv, &pin_mask, &long_mask,
				   trigger_tbt, dig_hotplug_reg,
				   dev_priv->display.hotplug.hpd,
				   gen11_port_hotplug_long_detect);
	}

	if (pin_mask)
		intel_hpd_irq_handler(dev_priv, pin_mask, long_mask);
	else
		drm_err(&dev_priv->drm,
			"Unexpected DE HPD interrupt 0x%08x\n", iir);
}

static u32 ibx_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_A:
		return PORTA_HOTPLUG_ENABLE;
	case HPD_PORT_B:
		return PORTB_HOTPLUG_ENABLE | PORTB_PULSE_DURATION_MASK;
	case HPD_PORT_C:
		return PORTC_HOTPLUG_ENABLE | PORTC_PULSE_DURATION_MASK;
	case HPD_PORT_D:
		return PORTD_HOTPLUG_ENABLE | PORTD_PULSE_DURATION_MASK;
	default:
		return 0;
	}
}

static u32 ibx_hotplug_enables(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	switch (encoder->hpd_pin) {
	case HPD_PORT_A:
		/*
		 * When CPU and PCH are on the same package, port A
		 * HPD must be enabled in both north and south.
		 */
		return HAS_PCH_LPT_LP(i915) ?
			PORTA_HOTPLUG_ENABLE : 0;
	case HPD_PORT_B:
		return PORTB_HOTPLUG_ENABLE |
			PORTB_PULSE_DURATION_2ms;
	case HPD_PORT_C:
		return PORTC_HOTPLUG_ENABLE |
			PORTC_PULSE_DURATION_2ms;
	case HPD_PORT_D:
		return PORTD_HOTPLUG_ENABLE |
			PORTD_PULSE_DURATION_2ms;
	default:
		return 0;
	}
}

static void ibx_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	/*
	 * Enable digital hotplug on the PCH, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec).
	 * The pulse duration bits are reserved on LPT+.
	 */
	intel_uncore_rmw(&dev_priv->uncore, PCH_PORT_HOTPLUG,
			 intel_hpd_hotplug_mask(dev_priv, ibx_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, ibx_hotplug_enables));
}

static void ibx_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_uncore_rmw(&i915->uncore, PCH_PORT_HOTPLUG,
			 ibx_hotplug_mask(encoder->hpd_pin),
			 ibx_hotplug_enables(encoder));
}

static void ibx_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	ibx_hpd_detection_setup(dev_priv);
}

static u32 icp_ddi_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_A:
	case HPD_PORT_B:
	case HPD_PORT_C:
	case HPD_PORT_D:
		return SHOTPLUG_CTL_DDI_HPD_ENABLE(hpd_pin);
	default:
		return 0;
	}
}

static u32 icp_ddi_hotplug_enables(struct intel_encoder *encoder)
{
	return icp_ddi_hotplug_mask(encoder->hpd_pin);
}

static u32 icp_tc_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return ICP_TC_HPD_ENABLE(hpd_pin);
	default:
		return 0;
	}
}

static u32 icp_tc_hotplug_enables(struct intel_encoder *encoder)
{
	return icp_tc_hotplug_mask(encoder->hpd_pin);
}

static void icp_ddi_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	intel_uncore_rmw(&dev_priv->uncore, SHOTPLUG_CTL_DDI,
			 intel_hpd_hotplug_mask(dev_priv, icp_ddi_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, icp_ddi_hotplug_enables));
}

static void icp_ddi_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_uncore_rmw(&i915->uncore, SHOTPLUG_CTL_DDI,
			 icp_ddi_hotplug_mask(encoder->hpd_pin),
			 icp_ddi_hotplug_enables(encoder));
}

static void icp_tc_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	intel_uncore_rmw(&dev_priv->uncore, SHOTPLUG_CTL_TC,
			 intel_hpd_hotplug_mask(dev_priv, icp_tc_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, icp_tc_hotplug_enables));
}

static void icp_tc_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_uncore_rmw(&i915->uncore, SHOTPLUG_CTL_TC,
			 icp_tc_hotplug_mask(encoder->hpd_pin),
			 icp_tc_hotplug_enables(encoder));
}

static void icp_hpd_enable_detection(struct intel_encoder *encoder)
{
	icp_ddi_hpd_enable_detection(encoder);
	icp_tc_hpd_enable_detection(encoder);
}

static void icp_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);

	if (INTEL_PCH_TYPE(dev_priv) <= PCH_TGP)
		intel_uncore_write(&dev_priv->uncore, SHPD_FILTER_CNT, SHPD_FILTER_CNT_500_ADJ);
	else
		intel_uncore_write(&dev_priv->uncore, SHPD_FILTER_CNT, SHPD_FILTER_CNT_250);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	icp_ddi_hpd_detection_setup(dev_priv);
	icp_tc_hpd_detection_setup(dev_priv);
}

static u32 gen11_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
	case HPD_PORT_TC5:
	case HPD_PORT_TC6:
		return GEN11_HOTPLUG_CTL_ENABLE(hpd_pin);
	default:
		return 0;
	}
}

static u32 gen11_hotplug_enables(struct intel_encoder *encoder)
{
	return gen11_hotplug_mask(encoder->hpd_pin);
}

static void dg1_hpd_invert(struct drm_i915_private *i915)
{
	u32 val = (INVERT_DDIA_HPD |
		   INVERT_DDIB_HPD |
		   INVERT_DDIC_HPD |
		   INVERT_DDID_HPD);
	intel_uncore_rmw(&i915->uncore, SOUTH_CHICKEN1, 0, val);
}

static void dg1_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	dg1_hpd_invert(i915);
	icp_hpd_enable_detection(encoder);
}

static void dg1_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	dg1_hpd_invert(dev_priv);
	icp_hpd_irq_setup(dev_priv);
}

static void gen11_tc_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	intel_uncore_rmw(&dev_priv->uncore, GEN11_TC_HOTPLUG_CTL,
			 intel_hpd_hotplug_mask(dev_priv, gen11_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, gen11_hotplug_enables));
}

static void gen11_tc_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_uncore_rmw(&i915->uncore, GEN11_TC_HOTPLUG_CTL,
			 gen11_hotplug_mask(encoder->hpd_pin),
			 gen11_hotplug_enables(encoder));
}

static void gen11_tbt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	intel_uncore_rmw(&dev_priv->uncore, GEN11_TBT_HOTPLUG_CTL,
			 intel_hpd_hotplug_mask(dev_priv, gen11_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, gen11_hotplug_enables));
}

static void gen11_tbt_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_uncore_rmw(&i915->uncore, GEN11_TBT_HOTPLUG_CTL,
			 gen11_hotplug_mask(encoder->hpd_pin),
			 gen11_hotplug_enables(encoder));
}

static void gen11_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	gen11_tc_hpd_enable_detection(encoder);
	gen11_tbt_hpd_enable_detection(encoder);

	if (INTEL_PCH_TYPE(i915) >= PCH_ICP)
		icp_hpd_enable_detection(encoder);
}

static void gen11_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.hpd);

	intel_uncore_rmw(&dev_priv->uncore, GEN11_DE_HPD_IMR, hotplug_irqs,
			 ~enabled_irqs & hotplug_irqs);
	intel_uncore_posting_read(&dev_priv->uncore, GEN11_DE_HPD_IMR);

	gen11_tc_hpd_detection_setup(dev_priv);
	gen11_tbt_hpd_detection_setup(dev_priv);

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_ICP)
		icp_hpd_irq_setup(dev_priv);
}

static u32 mtp_ddi_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_A:
	case HPD_PORT_B:
		return SHOTPLUG_CTL_DDI_HPD_ENABLE(hpd_pin);
	default:
		return 0;
	}
}

static u32 mtp_ddi_hotplug_enables(struct intel_encoder *encoder)
{
	return mtp_ddi_hotplug_mask(encoder->hpd_pin);
}

static u32 mtp_tc_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_TC1:
	case HPD_PORT_TC2:
	case HPD_PORT_TC3:
	case HPD_PORT_TC4:
		return ICP_TC_HPD_ENABLE(hpd_pin);
	default:
		return 0;
	}
}

static u32 mtp_tc_hotplug_enables(struct intel_encoder *encoder)
{
	return mtp_tc_hotplug_mask(encoder->hpd_pin);
}

static void mtp_ddi_hpd_detection_setup(struct drm_i915_private *i915)
{
	intel_de_rmw(i915, SHOTPLUG_CTL_DDI,
		     intel_hpd_hotplug_mask(i915, mtp_ddi_hotplug_mask),
		     intel_hpd_hotplug_enables(i915, mtp_ddi_hotplug_enables));
}

static void mtp_ddi_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_de_rmw(i915, SHOTPLUG_CTL_DDI,
		     mtp_ddi_hotplug_mask(encoder->hpd_pin),
		     mtp_ddi_hotplug_enables(encoder));
}

static void mtp_tc_hpd_detection_setup(struct drm_i915_private *i915)
{
	intel_de_rmw(i915, SHOTPLUG_CTL_TC,
		     intel_hpd_hotplug_mask(i915, mtp_tc_hotplug_mask),
		     intel_hpd_hotplug_enables(i915, mtp_tc_hotplug_enables));
}

static void mtp_tc_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_de_rmw(i915, SHOTPLUG_CTL_DDI,
		     mtp_tc_hotplug_mask(encoder->hpd_pin),
		     mtp_tc_hotplug_enables(encoder));
}

static void mtp_hpd_invert(struct drm_i915_private *i915)
{
	u32 val = (INVERT_DDIA_HPD |
		   INVERT_DDIB_HPD |
		   INVERT_DDIC_HPD |
		   INVERT_TC1_HPD |
		   INVERT_TC2_HPD |
		   INVERT_TC3_HPD |
		   INVERT_TC4_HPD |
		   INVERT_DDID_HPD_MTP |
		   INVERT_DDIE_HPD);
	intel_de_rmw(i915, SOUTH_CHICKEN1, 0, val);
}

static void mtp_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	mtp_hpd_invert(i915);
	mtp_ddi_hpd_enable_detection(encoder);
	mtp_tc_hpd_enable_detection(encoder);
}

static void mtp_hpd_irq_setup(struct drm_i915_private *i915)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(i915, i915->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(i915, i915->display.hotplug.pch_hpd);

	intel_de_write(i915, SHPD_FILTER_CNT, SHPD_FILTER_CNT_250);

	mtp_hpd_invert(i915);
	ibx_display_interrupt_update(i915, hotplug_irqs, enabled_irqs);

	mtp_ddi_hpd_detection_setup(i915);
	mtp_tc_hpd_detection_setup(i915);
}

static void xe2lpd_sde_hpd_irq_setup(struct drm_i915_private *i915)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(i915, i915->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(i915, i915->display.hotplug.pch_hpd);

	ibx_display_interrupt_update(i915, hotplug_irqs, enabled_irqs);

	mtp_ddi_hpd_detection_setup(i915);
	mtp_tc_hpd_detection_setup(i915);
}

static bool is_xelpdp_pica_hpd_pin(enum hpd_pin hpd_pin)
{
	return hpd_pin >= HPD_PORT_TC1 && hpd_pin <= HPD_PORT_TC4;
}

static void _xelpdp_pica_hpd_detection_setup(struct drm_i915_private *i915,
					     enum hpd_pin hpd_pin, bool enable)
{
	u32 mask = XELPDP_TBT_HOTPLUG_ENABLE |
		XELPDP_DP_ALT_HOTPLUG_ENABLE;

	if (!is_xelpdp_pica_hpd_pin(hpd_pin))
		return;

	intel_de_rmw(i915, XELPDP_PORT_HOTPLUG_CTL(hpd_pin),
		     mask, enable ? mask : 0);
}

static void xelpdp_pica_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	_xelpdp_pica_hpd_detection_setup(i915, encoder->hpd_pin, true);
}

static void xelpdp_pica_hpd_detection_setup(struct drm_i915_private *i915)
{
	struct intel_encoder *encoder;
	u32 available_pins = 0;
	enum hpd_pin pin;

	BUILD_BUG_ON(BITS_PER_TYPE(available_pins) < HPD_NUM_PINS);

	for_each_intel_encoder(&i915->drm, encoder)
		available_pins |= BIT(encoder->hpd_pin);

	for_each_hpd_pin(pin)
		_xelpdp_pica_hpd_detection_setup(i915, pin, available_pins & BIT(pin));
}

static void xelpdp_hpd_enable_detection(struct intel_encoder *encoder)
{
	xelpdp_pica_hpd_enable_detection(encoder);
	mtp_hpd_enable_detection(encoder);
}

static void xelpdp_hpd_irq_setup(struct drm_i915_private *i915)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(i915, i915->display.hotplug.hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(i915, i915->display.hotplug.hpd);

	intel_de_rmw(i915, PICAINTERRUPT_IMR, hotplug_irqs,
		     ~enabled_irqs & hotplug_irqs);
	intel_uncore_posting_read(&i915->uncore, PICAINTERRUPT_IMR);

	xelpdp_pica_hpd_detection_setup(i915);

	if (INTEL_PCH_TYPE(i915) >= PCH_LNL)
		xe2lpd_sde_hpd_irq_setup(i915);
	else if (INTEL_PCH_TYPE(i915) >= PCH_MTL)
		mtp_hpd_irq_setup(i915);
}

static u32 spt_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_A:
		return PORTA_HOTPLUG_ENABLE;
	case HPD_PORT_B:
		return PORTB_HOTPLUG_ENABLE;
	case HPD_PORT_C:
		return PORTC_HOTPLUG_ENABLE;
	case HPD_PORT_D:
		return PORTD_HOTPLUG_ENABLE;
	default:
		return 0;
	}
}

static u32 spt_hotplug_enables(struct intel_encoder *encoder)
{
	return spt_hotplug_mask(encoder->hpd_pin);
}

static u32 spt_hotplug2_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_E:
		return PORTE_HOTPLUG_ENABLE;
	default:
		return 0;
	}
}

static u32 spt_hotplug2_enables(struct intel_encoder *encoder)
{
	return spt_hotplug2_mask(encoder->hpd_pin);
}

static void spt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	/* Display WA #1179 WaHardHangonHotPlug: cnp */
	if (HAS_PCH_CNP(dev_priv)) {
		intel_uncore_rmw(&dev_priv->uncore, SOUTH_CHICKEN1, CHASSIS_CLK_REQ_DURATION_MASK,
				 CHASSIS_CLK_REQ_DURATION(0xf));
	}

	/* Enable digital hotplug on the PCH */
	intel_uncore_rmw(&dev_priv->uncore, PCH_PORT_HOTPLUG,
			 intel_hpd_hotplug_mask(dev_priv, spt_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, spt_hotplug_enables));

	intel_uncore_rmw(&dev_priv->uncore, PCH_PORT_HOTPLUG2,
			 intel_hpd_hotplug_mask(dev_priv, spt_hotplug2_mask),
			 intel_hpd_hotplug_enables(dev_priv, spt_hotplug2_enables));
}

static void spt_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	/* Display WA #1179 WaHardHangonHotPlug: cnp */
	if (HAS_PCH_CNP(i915)) {
		intel_uncore_rmw(&i915->uncore, SOUTH_CHICKEN1,
				 CHASSIS_CLK_REQ_DURATION_MASK,
				 CHASSIS_CLK_REQ_DURATION(0xf));
	}

	intel_uncore_rmw(&i915->uncore, PCH_PORT_HOTPLUG,
			 spt_hotplug_mask(encoder->hpd_pin),
			 spt_hotplug_enables(encoder));

	intel_uncore_rmw(&i915->uncore, PCH_PORT_HOTPLUG2,
			 spt_hotplug2_mask(encoder->hpd_pin),
			 spt_hotplug2_enables(encoder));
}

static void spt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	if (INTEL_PCH_TYPE(dev_priv) >= PCH_CNP)
		intel_uncore_write(&dev_priv->uncore, SHPD_FILTER_CNT, SHPD_FILTER_CNT_500_ADJ);

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.pch_hpd);

	ibx_display_interrupt_update(dev_priv, hotplug_irqs, enabled_irqs);

	spt_hpd_detection_setup(dev_priv);
}

static u32 ilk_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_A:
		return DIGITAL_PORTA_HOTPLUG_ENABLE |
			DIGITAL_PORTA_PULSE_DURATION_MASK;
	default:
		return 0;
	}
}

static u32 ilk_hotplug_enables(struct intel_encoder *encoder)
{
	switch (encoder->hpd_pin) {
	case HPD_PORT_A:
		return DIGITAL_PORTA_HOTPLUG_ENABLE |
			DIGITAL_PORTA_PULSE_DURATION_2ms;
	default:
		return 0;
	}
}

static void ilk_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	/*
	 * Enable digital hotplug on the CPU, and configure the DP short pulse
	 * duration to 2ms (which is the minimum in the Display Port spec)
	 * The pulse duration bits are reserved on HSW+.
	 */
	intel_uncore_rmw(&dev_priv->uncore, DIGITAL_PORT_HOTPLUG_CNTRL,
			 intel_hpd_hotplug_mask(dev_priv, ilk_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, ilk_hotplug_enables));
}

static void ilk_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_uncore_rmw(&i915->uncore, DIGITAL_PORT_HOTPLUG_CNTRL,
			 ilk_hotplug_mask(encoder->hpd_pin),
			 ilk_hotplug_enables(encoder));

	ibx_hpd_enable_detection(encoder);
}

static void ilk_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.hpd);

	if (DISPLAY_VER(dev_priv) >= 8)
		bdw_update_port_irq(dev_priv, hotplug_irqs, enabled_irqs);
	else
		ilk_update_display_irq(dev_priv, hotplug_irqs, enabled_irqs);

	ilk_hpd_detection_setup(dev_priv);

	ibx_hpd_irq_setup(dev_priv);
}

static u32 bxt_hotplug_mask(enum hpd_pin hpd_pin)
{
	switch (hpd_pin) {
	case HPD_PORT_A:
		return PORTA_HOTPLUG_ENABLE | BXT_DDIA_HPD_INVERT;
	case HPD_PORT_B:
		return PORTB_HOTPLUG_ENABLE | BXT_DDIB_HPD_INVERT;
	case HPD_PORT_C:
		return PORTC_HOTPLUG_ENABLE | BXT_DDIC_HPD_INVERT;
	default:
		return 0;
	}
}

static u32 bxt_hotplug_enables(struct intel_encoder *encoder)
{
	u32 hotplug;

	switch (encoder->hpd_pin) {
	case HPD_PORT_A:
		hotplug = PORTA_HOTPLUG_ENABLE;
		if (intel_bios_encoder_hpd_invert(encoder->devdata))
			hotplug |= BXT_DDIA_HPD_INVERT;
		return hotplug;
	case HPD_PORT_B:
		hotplug = PORTB_HOTPLUG_ENABLE;
		if (intel_bios_encoder_hpd_invert(encoder->devdata))
			hotplug |= BXT_DDIB_HPD_INVERT;
		return hotplug;
	case HPD_PORT_C:
		hotplug = PORTC_HOTPLUG_ENABLE;
		if (intel_bios_encoder_hpd_invert(encoder->devdata))
			hotplug |= BXT_DDIC_HPD_INVERT;
		return hotplug;
	default:
		return 0;
	}
}

static void bxt_hpd_detection_setup(struct drm_i915_private *dev_priv)
{
	intel_uncore_rmw(&dev_priv->uncore, PCH_PORT_HOTPLUG,
			 intel_hpd_hotplug_mask(dev_priv, bxt_hotplug_mask),
			 intel_hpd_hotplug_enables(dev_priv, bxt_hotplug_enables));
}

static void bxt_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_uncore_rmw(&i915->uncore, PCH_PORT_HOTPLUG,
			 bxt_hotplug_mask(encoder->hpd_pin),
			 bxt_hotplug_enables(encoder));
}

static void bxt_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_irqs, enabled_irqs;

	enabled_irqs = intel_hpd_enabled_irqs(dev_priv, dev_priv->display.hotplug.hpd);
	hotplug_irqs = intel_hpd_hotplug_irqs(dev_priv, dev_priv->display.hotplug.hpd);

	bdw_update_port_irq(dev_priv, hotplug_irqs, enabled_irqs);

	bxt_hpd_detection_setup(dev_priv);
}

static void g45_hpd_peg_band_gap_wa(struct drm_i915_private *i915)
{
	/*
	 * For G4X desktop chip, PEG_BAND_GAP_DATA 3:0 must first be written
	 * 0xd.  Failure to do so will result in spurious interrupts being
	 * generated on the port when a cable is not attached.
	 */
	intel_de_rmw(i915, PEG_BAND_GAP_DATA, 0xf, 0xd);
}

static void i915_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u32 hotplug_en = hpd_mask_i915[encoder->hpd_pin];

	if (IS_G45(i915))
		g45_hpd_peg_band_gap_wa(i915);

	/* HPD sense and interrupt enable are one and the same */
	i915_hotplug_interrupt_update(i915, hotplug_en, hotplug_en);
}

static void i915_hpd_irq_setup(struct drm_i915_private *dev_priv)
{
	u32 hotplug_en;

	lockdep_assert_held(&dev_priv->irq_lock);

	/*
	 * Note HDMI and DP share hotplug bits. Enable bits are the same for all
	 * generations.
	 */
	hotplug_en = intel_hpd_enabled_irqs(dev_priv, hpd_mask_i915);
	/*
	 * Programming the CRT detection parameters tends to generate a spurious
	 * hotplug event about three seconds later. So just do it once.
	 */
	if (IS_G4X(dev_priv))
		hotplug_en |= CRT_HOTPLUG_ACTIVATION_PERIOD_64;
	hotplug_en |= CRT_HOTPLUG_VOLTAGE_COMPARE_50;

	if (IS_G45(dev_priv))
		g45_hpd_peg_band_gap_wa(dev_priv);

	/* Ignore TV since it's buggy */
	i915_hotplug_interrupt_update_locked(dev_priv,
					     HOTPLUG_INT_EN_MASK |
					     CRT_HOTPLUG_VOLTAGE_COMPARE_MASK |
					     CRT_HOTPLUG_ACTIVATION_PERIOD_64,
					     hotplug_en);
}

struct intel_hotplug_funcs {
	/* Enable HPD sense and interrupts for all present encoders */
	void (*hpd_irq_setup)(struct drm_i915_private *i915);
	/* Enable HPD sense for a single encoder */
	void (*hpd_enable_detection)(struct intel_encoder *encoder);
};

#define HPD_FUNCS(platform)					 \
static const struct intel_hotplug_funcs platform##_hpd_funcs = { \
	.hpd_irq_setup = platform##_hpd_irq_setup,		 \
	.hpd_enable_detection = platform##_hpd_enable_detection, \
}

HPD_FUNCS(i915);
HPD_FUNCS(xelpdp);
HPD_FUNCS(dg1);
HPD_FUNCS(gen11);
HPD_FUNCS(bxt);
HPD_FUNCS(icp);
HPD_FUNCS(spt);
HPD_FUNCS(ilk);
#undef HPD_FUNCS

void intel_hpd_enable_detection(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	if (i915->display.funcs.hotplug)
		i915->display.funcs.hotplug->hpd_enable_detection(encoder);
}

void intel_hpd_irq_setup(struct drm_i915_private *i915)
{
	if (i915->display.irq.display_irqs_enabled && i915->display.funcs.hotplug)
		i915->display.funcs.hotplug->hpd_irq_setup(i915);
}

void intel_hotplug_irq_init(struct drm_i915_private *i915)
{
	intel_hpd_init_pins(i915);

	intel_hpd_init_early(i915);

	if (HAS_GMCH(i915)) {
		if (I915_HAS_HOTPLUG(i915))
			i915->display.funcs.hotplug = &i915_hpd_funcs;
	} else {
		if (HAS_PCH_DG2(i915))
			i915->display.funcs.hotplug = &icp_hpd_funcs;
		else if (HAS_PCH_DG1(i915))
			i915->display.funcs.hotplug = &dg1_hpd_funcs;
		else if (DISPLAY_VER(i915) >= 14)
			i915->display.funcs.hotplug = &xelpdp_hpd_funcs;
		else if (DISPLAY_VER(i915) >= 11)
			i915->display.funcs.hotplug = &gen11_hpd_funcs;
		else if (IS_GEMINILAKE(i915) || IS_BROXTON(i915))
			i915->display.funcs.hotplug = &bxt_hpd_funcs;
		else if (INTEL_PCH_TYPE(i915) >= PCH_ICP)
			i915->display.funcs.hotplug = &icp_hpd_funcs;
		else if (INTEL_PCH_TYPE(i915) >= PCH_SPT)
			i915->display.funcs.hotplug = &spt_hpd_funcs;
		else
			i915->display.funcs.hotplug = &ilk_hpd_funcs;
	}
}
