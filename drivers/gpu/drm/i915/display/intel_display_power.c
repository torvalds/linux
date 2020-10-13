/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#include "display/intel_crt.h"
#include "display/intel_dp.h"

#include "i915_drv.h"
#include "i915_irq.h"
#include "intel_cdclk.h"
#include "intel_combo_phy.h"
#include "intel_csr.h"
#include "intel_display_power.h"
#include "intel_display_types.h"
#include "intel_dpio_phy.h"
#include "intel_hotplug.h"
#include "intel_pm.h"
#include "intel_sideband.h"
#include "intel_tc.h"
#include "intel_vga.h"

bool intel_display_power_well_is_enabled(struct drm_i915_private *dev_priv,
					 enum i915_power_well_id power_well_id);

const char *
intel_display_power_domain_str(enum intel_display_power_domain domain)
{
	switch (domain) {
	case POWER_DOMAIN_DISPLAY_CORE:
		return "DISPLAY_CORE";
	case POWER_DOMAIN_PIPE_A:
		return "PIPE_A";
	case POWER_DOMAIN_PIPE_B:
		return "PIPE_B";
	case POWER_DOMAIN_PIPE_C:
		return "PIPE_C";
	case POWER_DOMAIN_PIPE_D:
		return "PIPE_D";
	case POWER_DOMAIN_PIPE_A_PANEL_FITTER:
		return "PIPE_A_PANEL_FITTER";
	case POWER_DOMAIN_PIPE_B_PANEL_FITTER:
		return "PIPE_B_PANEL_FITTER";
	case POWER_DOMAIN_PIPE_C_PANEL_FITTER:
		return "PIPE_C_PANEL_FITTER";
	case POWER_DOMAIN_PIPE_D_PANEL_FITTER:
		return "PIPE_D_PANEL_FITTER";
	case POWER_DOMAIN_TRANSCODER_A:
		return "TRANSCODER_A";
	case POWER_DOMAIN_TRANSCODER_B:
		return "TRANSCODER_B";
	case POWER_DOMAIN_TRANSCODER_C:
		return "TRANSCODER_C";
	case POWER_DOMAIN_TRANSCODER_D:
		return "TRANSCODER_D";
	case POWER_DOMAIN_TRANSCODER_EDP:
		return "TRANSCODER_EDP";
	case POWER_DOMAIN_TRANSCODER_VDSC_PW2:
		return "TRANSCODER_VDSC_PW2";
	case POWER_DOMAIN_TRANSCODER_DSI_A:
		return "TRANSCODER_DSI_A";
	case POWER_DOMAIN_TRANSCODER_DSI_C:
		return "TRANSCODER_DSI_C";
	case POWER_DOMAIN_PORT_DDI_A_LANES:
		return "PORT_DDI_A_LANES";
	case POWER_DOMAIN_PORT_DDI_B_LANES:
		return "PORT_DDI_B_LANES";
	case POWER_DOMAIN_PORT_DDI_C_LANES:
		return "PORT_DDI_C_LANES";
	case POWER_DOMAIN_PORT_DDI_D_LANES:
		return "PORT_DDI_D_LANES";
	case POWER_DOMAIN_PORT_DDI_E_LANES:
		return "PORT_DDI_E_LANES";
	case POWER_DOMAIN_PORT_DDI_F_LANES:
		return "PORT_DDI_F_LANES";
	case POWER_DOMAIN_PORT_DDI_G_LANES:
		return "PORT_DDI_G_LANES";
	case POWER_DOMAIN_PORT_DDI_H_LANES:
		return "PORT_DDI_H_LANES";
	case POWER_DOMAIN_PORT_DDI_I_LANES:
		return "PORT_DDI_I_LANES";
	case POWER_DOMAIN_PORT_DDI_A_IO:
		return "PORT_DDI_A_IO";
	case POWER_DOMAIN_PORT_DDI_B_IO:
		return "PORT_DDI_B_IO";
	case POWER_DOMAIN_PORT_DDI_C_IO:
		return "PORT_DDI_C_IO";
	case POWER_DOMAIN_PORT_DDI_D_IO:
		return "PORT_DDI_D_IO";
	case POWER_DOMAIN_PORT_DDI_E_IO:
		return "PORT_DDI_E_IO";
	case POWER_DOMAIN_PORT_DDI_F_IO:
		return "PORT_DDI_F_IO";
	case POWER_DOMAIN_PORT_DDI_G_IO:
		return "PORT_DDI_G_IO";
	case POWER_DOMAIN_PORT_DDI_H_IO:
		return "PORT_DDI_H_IO";
	case POWER_DOMAIN_PORT_DDI_I_IO:
		return "PORT_DDI_I_IO";
	case POWER_DOMAIN_PORT_DSI:
		return "PORT_DSI";
	case POWER_DOMAIN_PORT_CRT:
		return "PORT_CRT";
	case POWER_DOMAIN_PORT_OTHER:
		return "PORT_OTHER";
	case POWER_DOMAIN_VGA:
		return "VGA";
	case POWER_DOMAIN_AUDIO:
		return "AUDIO";
	case POWER_DOMAIN_AUX_A:
		return "AUX_A";
	case POWER_DOMAIN_AUX_B:
		return "AUX_B";
	case POWER_DOMAIN_AUX_C:
		return "AUX_C";
	case POWER_DOMAIN_AUX_D:
		return "AUX_D";
	case POWER_DOMAIN_AUX_E:
		return "AUX_E";
	case POWER_DOMAIN_AUX_F:
		return "AUX_F";
	case POWER_DOMAIN_AUX_G:
		return "AUX_G";
	case POWER_DOMAIN_AUX_H:
		return "AUX_H";
	case POWER_DOMAIN_AUX_I:
		return "AUX_I";
	case POWER_DOMAIN_AUX_IO_A:
		return "AUX_IO_A";
	case POWER_DOMAIN_AUX_C_TBT:
		return "AUX_C_TBT";
	case POWER_DOMAIN_AUX_D_TBT:
		return "AUX_D_TBT";
	case POWER_DOMAIN_AUX_E_TBT:
		return "AUX_E_TBT";
	case POWER_DOMAIN_AUX_F_TBT:
		return "AUX_F_TBT";
	case POWER_DOMAIN_AUX_G_TBT:
		return "AUX_G_TBT";
	case POWER_DOMAIN_AUX_H_TBT:
		return "AUX_H_TBT";
	case POWER_DOMAIN_AUX_I_TBT:
		return "AUX_I_TBT";
	case POWER_DOMAIN_GMBUS:
		return "GMBUS";
	case POWER_DOMAIN_INIT:
		return "INIT";
	case POWER_DOMAIN_MODESET:
		return "MODESET";
	case POWER_DOMAIN_GT_IRQ:
		return "GT_IRQ";
	case POWER_DOMAIN_DPLL_DC_OFF:
		return "DPLL_DC_OFF";
	case POWER_DOMAIN_TC_COLD_OFF:
		return "TC_COLD_OFF";
	default:
		MISSING_CASE(domain);
		return "?";
	}
}

static void intel_power_well_enable(struct drm_i915_private *dev_priv,
				    struct i915_power_well *power_well)
{
	drm_dbg_kms(&dev_priv->drm, "enabling %s\n", power_well->desc->name);
	power_well->desc->ops->enable(dev_priv, power_well);
	power_well->hw_enabled = true;
}

static void intel_power_well_disable(struct drm_i915_private *dev_priv,
				     struct i915_power_well *power_well)
{
	drm_dbg_kms(&dev_priv->drm, "disabling %s\n", power_well->desc->name);
	power_well->hw_enabled = false;
	power_well->desc->ops->disable(dev_priv, power_well);
}

static void intel_power_well_get(struct drm_i915_private *dev_priv,
				 struct i915_power_well *power_well)
{
	if (!power_well->count++)
		intel_power_well_enable(dev_priv, power_well);
}

static void intel_power_well_put(struct drm_i915_private *dev_priv,
				 struct i915_power_well *power_well)
{
	drm_WARN(&dev_priv->drm, !power_well->count,
		 "Use count on power well %s is already zero",
		 power_well->desc->name);

	if (!--power_well->count)
		intel_power_well_disable(dev_priv, power_well);
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
	struct i915_power_well *power_well;
	bool is_enabled;

	if (dev_priv->runtime_pm.suspended)
		return false;

	is_enabled = true;

	for_each_power_domain_well_reverse(dev_priv, power_well, BIT_ULL(domain)) {
		if (power_well->desc->always_on)
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

/*
 * Starting with Haswell, we have a "Power Down Well" that can be turned off
 * when not needed anymore. We have 4 registers that can request the power well
 * to be enabled, and it will only be disabled if none of the registers is
 * requesting it to be enabled.
 */
static void hsw_power_well_post_enable(struct drm_i915_private *dev_priv,
				       u8 irq_pipe_mask, bool has_vga)
{
	if (has_vga)
		intel_vga_reset_io_mem(dev_priv);

	if (irq_pipe_mask)
		gen8_irq_power_well_post_enable(dev_priv, irq_pipe_mask);
}

static void hsw_power_well_pre_disable(struct drm_i915_private *dev_priv,
				       u8 irq_pipe_mask)
{
	if (irq_pipe_mask)
		gen8_irq_power_well_pre_disable(dev_priv, irq_pipe_mask);
}

#define ICL_AUX_PW_TO_CH(pw_idx)	\
	((pw_idx) - ICL_PW_CTL_IDX_AUX_A + AUX_CH_A)

#define ICL_TBT_AUX_PW_TO_CH(pw_idx)	\
	((pw_idx) - ICL_PW_CTL_IDX_AUX_TBT1 + AUX_CH_C)

static enum aux_ch icl_tc_phy_aux_ch(struct drm_i915_private *dev_priv,
				     struct i915_power_well *power_well)
{
	int pw_idx = power_well->desc->hsw.idx;

	return power_well->desc->hsw.is_tc_tbt ? ICL_TBT_AUX_PW_TO_CH(pw_idx) :
						 ICL_AUX_PW_TO_CH(pw_idx);
}

static struct intel_digital_port *
aux_ch_to_digital_port(struct drm_i915_private *dev_priv,
		       enum aux_ch aux_ch)
{
	struct intel_digital_port *dig_port = NULL;
	struct intel_encoder *encoder;

	for_each_intel_encoder(&dev_priv->drm, encoder) {
		/* We'll check the MST primary port */
		if (encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		dig_port = enc_to_dig_port(encoder);
		if (!dig_port)
			continue;

		if (dig_port->aux_ch != aux_ch) {
			dig_port = NULL;
			continue;
		}

		break;
	}

	return dig_port;
}

static void hsw_wait_for_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well,
					   bool timeout_expected)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;

	/* Timeout for PW1:10 us, AUX:not specified, other PWs:20 us. */
	if (intel_de_wait_for_set(dev_priv, regs->driver,
				  HSW_PWR_WELL_CTL_STATE(pw_idx), 1)) {
		drm_dbg_kms(&dev_priv->drm, "%s power well enable timeout\n",
			    power_well->desc->name);

		drm_WARN_ON(&dev_priv->drm, !timeout_expected);

	}
}

static u32 hsw_power_well_requesters(struct drm_i915_private *dev_priv,
				     const struct i915_power_well_regs *regs,
				     int pw_idx)
{
	u32 req_mask = HSW_PWR_WELL_CTL_REQ(pw_idx);
	u32 ret;

	ret = intel_de_read(dev_priv, regs->bios) & req_mask ? 1 : 0;
	ret |= intel_de_read(dev_priv, regs->driver) & req_mask ? 2 : 0;
	if (regs->kvmr.reg)
		ret |= intel_de_read(dev_priv, regs->kvmr) & req_mask ? 4 : 0;
	ret |= intel_de_read(dev_priv, regs->debug) & req_mask ? 8 : 0;

	return ret;
}

static void hsw_wait_for_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	bool disabled;
	u32 reqs;

	/*
	 * Bspec doesn't require waiting for PWs to get disabled, but still do
	 * this for paranoia. The known cases where a PW will be forced on:
	 * - a KVMR request on any power well via the KVMR request register
	 * - a DMC request on PW1 and MISC_IO power wells via the BIOS and
	 *   DEBUG request registers
	 * Skip the wait in case any of the request bits are set and print a
	 * diagnostic message.
	 */
	wait_for((disabled = !(intel_de_read(dev_priv, regs->driver) &
			       HSW_PWR_WELL_CTL_STATE(pw_idx))) ||
		 (reqs = hsw_power_well_requesters(dev_priv, regs, pw_idx)), 1);
	if (disabled)
		return;

	drm_dbg_kms(&dev_priv->drm,
		    "%s forced on (bios:%d driver:%d kvmr:%d debug:%d)\n",
		    power_well->desc->name,
		    !!(reqs & 1), !!(reqs & 2), !!(reqs & 4), !!(reqs & 8));
}

static void gen9_wait_for_power_well_fuses(struct drm_i915_private *dev_priv,
					   enum skl_power_gate pg)
{
	/* Timeout 5us for PG#0, for other PGs 1us */
	drm_WARN_ON(&dev_priv->drm,
		    intel_de_wait_for_set(dev_priv, SKL_FUSE_STATUS,
					  SKL_FUSE_PG_DIST_STATUS(pg), 1));
}

static void hsw_power_well_enable(struct drm_i915_private *dev_priv,
				  struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	u32 val;

	if (power_well->desc->hsw.has_fuses) {
		enum skl_power_gate pg;

		pg = INTEL_GEN(dev_priv) >= 11 ? ICL_PW_CTL_IDX_TO_PG(pw_idx) :
						 SKL_PW_CTL_IDX_TO_PG(pw_idx);
		/*
		 * For PW1 we have to wait both for the PW0/PG0 fuse state
		 * before enabling the power well and PW1/PG1's own fuse
		 * state after the enabling. For all other power wells with
		 * fuses we only have to wait for that PW/PG's fuse state
		 * after the enabling.
		 */
		if (pg == SKL_PG1)
			gen9_wait_for_power_well_fuses(dev_priv, SKL_PG0);
	}

	val = intel_de_read(dev_priv, regs->driver);
	intel_de_write(dev_priv, regs->driver,
		       val | HSW_PWR_WELL_CTL_REQ(pw_idx));

	hsw_wait_for_power_well_enable(dev_priv, power_well, false);

	/* Display WA #1178: cnl */
	if (IS_CANNONLAKE(dev_priv) &&
	    pw_idx >= GLK_PW_CTL_IDX_AUX_B &&
	    pw_idx <= CNL_PW_CTL_IDX_AUX_F) {
		u32 val;

		val = intel_de_read(dev_priv, CNL_AUX_ANAOVRD1(pw_idx));
		val |= CNL_AUX_ANAOVRD1_ENABLE | CNL_AUX_ANAOVRD1_LDO_BYPASS;
		intel_de_write(dev_priv, CNL_AUX_ANAOVRD1(pw_idx), val);
	}

	if (power_well->desc->hsw.has_fuses) {
		enum skl_power_gate pg;

		pg = INTEL_GEN(dev_priv) >= 11 ? ICL_PW_CTL_IDX_TO_PG(pw_idx) :
						 SKL_PW_CTL_IDX_TO_PG(pw_idx);
		gen9_wait_for_power_well_fuses(dev_priv, pg);
	}

	hsw_power_well_post_enable(dev_priv,
				   power_well->desc->hsw.irq_pipe_mask,
				   power_well->desc->hsw.has_vga);
}

static void hsw_power_well_disable(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	u32 val;

	hsw_power_well_pre_disable(dev_priv,
				   power_well->desc->hsw.irq_pipe_mask);

	val = intel_de_read(dev_priv, regs->driver);
	intel_de_write(dev_priv, regs->driver,
		       val & ~HSW_PWR_WELL_CTL_REQ(pw_idx));
	hsw_wait_for_power_well_disable(dev_priv, power_well);
}

#define ICL_AUX_PW_TO_PHY(pw_idx)	((pw_idx) - ICL_PW_CTL_IDX_AUX_A)

static void
icl_combo_phy_aux_power_well_enable(struct drm_i915_private *dev_priv,
				    struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	enum phy phy = ICL_AUX_PW_TO_PHY(pw_idx);
	u32 val;

	drm_WARN_ON(&dev_priv->drm, !IS_ICELAKE(dev_priv));

	val = intel_de_read(dev_priv, regs->driver);
	intel_de_write(dev_priv, regs->driver,
		       val | HSW_PWR_WELL_CTL_REQ(pw_idx));

	if (INTEL_GEN(dev_priv) < 12) {
		val = intel_de_read(dev_priv, ICL_PORT_CL_DW12(phy));
		intel_de_write(dev_priv, ICL_PORT_CL_DW12(phy),
			       val | ICL_LANE_ENABLE_AUX);
	}

	hsw_wait_for_power_well_enable(dev_priv, power_well, false);

	/* Display WA #1178: icl */
	if (pw_idx >= ICL_PW_CTL_IDX_AUX_A && pw_idx <= ICL_PW_CTL_IDX_AUX_B &&
	    !intel_bios_is_port_edp(dev_priv, (enum port)phy)) {
		val = intel_de_read(dev_priv, ICL_AUX_ANAOVRD1(pw_idx));
		val |= ICL_AUX_ANAOVRD1_ENABLE | ICL_AUX_ANAOVRD1_LDO_BYPASS;
		intel_de_write(dev_priv, ICL_AUX_ANAOVRD1(pw_idx), val);
	}
}

static void
icl_combo_phy_aux_power_well_disable(struct drm_i915_private *dev_priv,
				     struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	enum phy phy = ICL_AUX_PW_TO_PHY(pw_idx);
	u32 val;

	drm_WARN_ON(&dev_priv->drm, !IS_ICELAKE(dev_priv));

	val = intel_de_read(dev_priv, ICL_PORT_CL_DW12(phy));
	intel_de_write(dev_priv, ICL_PORT_CL_DW12(phy),
		       val & ~ICL_LANE_ENABLE_AUX);

	val = intel_de_read(dev_priv, regs->driver);
	intel_de_write(dev_priv, regs->driver,
		       val & ~HSW_PWR_WELL_CTL_REQ(pw_idx));

	hsw_wait_for_power_well_disable(dev_priv, power_well);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)

static u64 async_put_domains_mask(struct i915_power_domains *power_domains);

static int power_well_async_ref_count(struct drm_i915_private *dev_priv,
				      struct i915_power_well *power_well)
{
	int refs = hweight64(power_well->desc->domains &
			     async_put_domains_mask(&dev_priv->power_domains));

	drm_WARN_ON(&dev_priv->drm, refs > power_well->count);

	return refs;
}

static void icl_tc_port_assert_ref_held(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well,
					struct intel_digital_port *dig_port)
{
	/* Bypass the check if all references are released asynchronously */
	if (power_well_async_ref_count(dev_priv, power_well) ==
	    power_well->count)
		return;

	if (drm_WARN_ON(&dev_priv->drm, !dig_port))
		return;

	if (INTEL_GEN(dev_priv) == 11 && dig_port->tc_legacy_port)
		return;

	drm_WARN_ON(&dev_priv->drm, !intel_tc_port_ref_held(dig_port));
}

#else

static void icl_tc_port_assert_ref_held(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well,
					struct intel_digital_port *dig_port)
{
}

#endif

#define TGL_AUX_PW_TO_TC_PORT(pw_idx)	((pw_idx) - TGL_PW_CTL_IDX_AUX_TC1)

static void icl_tc_cold_exit(struct drm_i915_private *i915)
{
	int ret, tries = 0;

	while (1) {
		ret = sandybridge_pcode_write_timeout(i915,
						      ICL_PCODE_EXIT_TCCOLD,
						      0, 250, 1);
		if (ret != -EAGAIN || ++tries == 3)
			break;
		msleep(1);
	}

	/* Spec states that TC cold exit can take up to 1ms to complete */
	if (!ret)
		msleep(1);

	/* TODO: turn failure into a error as soon i915 CI updates ICL IFWI */
	drm_dbg_kms(&i915->drm, "TC cold block %s\n", ret ? "failed" :
		    "succeeded");
}

static void
icl_tc_phy_aux_power_well_enable(struct drm_i915_private *dev_priv,
				 struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = icl_tc_phy_aux_ch(dev_priv, power_well);
	struct intel_digital_port *dig_port = aux_ch_to_digital_port(dev_priv, aux_ch);
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	bool is_tbt = power_well->desc->hsw.is_tc_tbt;
	bool timeout_expected;
	u32 val;

	icl_tc_port_assert_ref_held(dev_priv, power_well, dig_port);

	val = intel_de_read(dev_priv, DP_AUX_CH_CTL(aux_ch));
	val &= ~DP_AUX_CH_CTL_TBT_IO;
	if (is_tbt)
		val |= DP_AUX_CH_CTL_TBT_IO;
	intel_de_write(dev_priv, DP_AUX_CH_CTL(aux_ch), val);

	val = intel_de_read(dev_priv, regs->driver);
	intel_de_write(dev_priv, regs->driver,
		       val | HSW_PWR_WELL_CTL_REQ(power_well->desc->hsw.idx));

	/*
	 * An AUX timeout is expected if the TBT DP tunnel is down,
	 * or need to enable AUX on a legacy TypeC port as part of the TC-cold
	 * exit sequence.
	 */
	timeout_expected = is_tbt;
	if (INTEL_GEN(dev_priv) == 11 && dig_port->tc_legacy_port) {
		icl_tc_cold_exit(dev_priv);
		timeout_expected = true;
	}

	hsw_wait_for_power_well_enable(dev_priv, power_well, timeout_expected);

	if (INTEL_GEN(dev_priv) >= 12 && !is_tbt) {
		enum tc_port tc_port;

		tc_port = TGL_AUX_PW_TO_TC_PORT(power_well->desc->hsw.idx);
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x2));

		if (intel_de_wait_for_set(dev_priv, DKL_CMN_UC_DW_27(tc_port),
					  DKL_CMN_UC_DW27_UC_HEALTH, 1))
			drm_warn(&dev_priv->drm,
				 "Timeout waiting TC uC health\n");
	}
}

static void
icl_tc_phy_aux_power_well_disable(struct drm_i915_private *dev_priv,
				  struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = icl_tc_phy_aux_ch(dev_priv, power_well);
	struct intel_digital_port *dig_port = aux_ch_to_digital_port(dev_priv, aux_ch);

	icl_tc_port_assert_ref_held(dev_priv, power_well, dig_port);

	hsw_power_well_disable(dev_priv, power_well);
}

static void
icl_aux_power_well_enable(struct drm_i915_private *dev_priv,
			  struct i915_power_well *power_well)
{
	int pw_idx = power_well->desc->hsw.idx;
	enum phy phy = ICL_AUX_PW_TO_PHY(pw_idx);  /* non-TBT only */
	bool is_tbt = power_well->desc->hsw.is_tc_tbt;

	if (is_tbt || intel_phy_is_tc(dev_priv, phy))
		return icl_tc_phy_aux_power_well_enable(dev_priv, power_well);
	else if (IS_ICELAKE(dev_priv))
		return icl_combo_phy_aux_power_well_enable(dev_priv,
							   power_well);
	else
		return hsw_power_well_enable(dev_priv, power_well);
}

static void
icl_aux_power_well_disable(struct drm_i915_private *dev_priv,
			   struct i915_power_well *power_well)
{
	int pw_idx = power_well->desc->hsw.idx;
	enum phy phy = ICL_AUX_PW_TO_PHY(pw_idx);  /* non-TBT only */
	bool is_tbt = power_well->desc->hsw.is_tc_tbt;

	if (is_tbt || intel_phy_is_tc(dev_priv, phy))
		return icl_tc_phy_aux_power_well_disable(dev_priv, power_well);
	else if (IS_ICELAKE(dev_priv))
		return icl_combo_phy_aux_power_well_disable(dev_priv,
							    power_well);
	else
		return hsw_power_well_disable(dev_priv, power_well);
}

/*
 * We should only use the power well if we explicitly asked the hardware to
 * enable it, so check if it's enabled and also check if we've requested it to
 * be enabled.
 */
static bool hsw_power_well_enabled(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	enum i915_power_well_id id = power_well->desc->id;
	int pw_idx = power_well->desc->hsw.idx;
	u32 mask = HSW_PWR_WELL_CTL_REQ(pw_idx) |
		   HSW_PWR_WELL_CTL_STATE(pw_idx);
	u32 val;

	val = intel_de_read(dev_priv, regs->driver);

	/*
	 * On GEN9 big core due to a DMC bug the driver's request bits for PW1
	 * and the MISC_IO PW will be not restored, so check instead for the
	 * BIOS's own request bits, which are forced-on for these power wells
	 * when exiting DC5/6.
	 */
	if (IS_GEN(dev_priv, 9) && !IS_GEN9_LP(dev_priv) &&
	    (id == SKL_DISP_PW_1 || id == SKL_DISP_PW_MISC_IO))
		val |= intel_de_read(dev_priv, regs->bios);

	return (val & mask) == mask;
}

static void assert_can_enable_dc9(struct drm_i915_private *dev_priv)
{
	drm_WARN_ONCE(&dev_priv->drm,
		      (intel_de_read(dev_priv, DC_STATE_EN) & DC_STATE_EN_DC9),
		      "DC9 already programmed to be enabled.\n");
	drm_WARN_ONCE(&dev_priv->drm,
		      intel_de_read(dev_priv, DC_STATE_EN) &
		      DC_STATE_EN_UPTO_DC5,
		      "DC5 still not disabled to enable DC9.\n");
	drm_WARN_ONCE(&dev_priv->drm,
		      intel_de_read(dev_priv, HSW_PWR_WELL_CTL2) &
		      HSW_PWR_WELL_CTL_REQ(SKL_PW_CTL_IDX_PW_2),
		      "Power well 2 on.\n");
	drm_WARN_ONCE(&dev_priv->drm, intel_irqs_enabled(dev_priv),
		      "Interrupts not disabled yet.\n");

	 /*
	  * TODO: check for the following to verify the conditions to enter DC9
	  * state are satisfied:
	  * 1] Check relevant display engine registers to verify if mode set
	  * disable sequence was followed.
	  * 2] Check if display uninitialize sequence is initialized.
	  */
}

static void assert_can_disable_dc9(struct drm_i915_private *dev_priv)
{
	drm_WARN_ONCE(&dev_priv->drm, intel_irqs_enabled(dev_priv),
		      "Interrupts not disabled yet.\n");
	drm_WARN_ONCE(&dev_priv->drm,
		      intel_de_read(dev_priv, DC_STATE_EN) &
		      DC_STATE_EN_UPTO_DC5,
		      "DC5 still not disabled.\n");

	 /*
	  * TODO: check for the following to verify DC9 state was indeed
	  * entered before programming to disable it:
	  * 1] Check relevant display engine registers to verify if mode
	  *  set disable sequence was followed.
	  * 2] Check if display uninitialize sequence is initialized.
	  */
}

static void gen9_write_dc_state(struct drm_i915_private *dev_priv,
				u32 state)
{
	int rewrites = 0;
	int rereads = 0;
	u32 v;

	intel_de_write(dev_priv, DC_STATE_EN, state);

	/* It has been observed that disabling the dc6 state sometimes
	 * doesn't stick and dmc keeps returning old value. Make sure
	 * the write really sticks enough times and also force rewrite until
	 * we are confident that state is exactly what we want.
	 */
	do  {
		v = intel_de_read(dev_priv, DC_STATE_EN);

		if (v != state) {
			intel_de_write(dev_priv, DC_STATE_EN, state);
			rewrites++;
			rereads = 0;
		} else if (rereads++ > 5) {
			break;
		}

	} while (rewrites < 100);

	if (v != state)
		drm_err(&dev_priv->drm,
			"Writing dc state to 0x%x failed, now 0x%x\n",
			state, v);

	/* Most of the times we need one retry, avoid spam */
	if (rewrites > 1)
		drm_dbg_kms(&dev_priv->drm,
			    "Rewrote dc state to 0x%x %d times\n",
			    state, rewrites);
}

static u32 gen9_dc_mask(struct drm_i915_private *dev_priv)
{
	u32 mask;

	mask = DC_STATE_EN_UPTO_DC5;

	if (INTEL_GEN(dev_priv) >= 12)
		mask |= DC_STATE_EN_DC3CO | DC_STATE_EN_UPTO_DC6
					  | DC_STATE_EN_DC9;
	else if (IS_GEN(dev_priv, 11))
		mask |= DC_STATE_EN_UPTO_DC6 | DC_STATE_EN_DC9;
	else if (IS_GEN9_LP(dev_priv))
		mask |= DC_STATE_EN_DC9;
	else
		mask |= DC_STATE_EN_UPTO_DC6;

	return mask;
}

static void gen9_sanitize_dc_state(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = intel_de_read(dev_priv, DC_STATE_EN) & gen9_dc_mask(dev_priv);

	drm_dbg_kms(&dev_priv->drm,
		    "Resetting DC state tracking from %02x to %02x\n",
		    dev_priv->csr.dc_state, val);
	dev_priv->csr.dc_state = val;
}

/**
 * gen9_set_dc_state - set target display C power state
 * @dev_priv: i915 device instance
 * @state: target DC power state
 * - DC_STATE_DISABLE
 * - DC_STATE_EN_UPTO_DC5
 * - DC_STATE_EN_UPTO_DC6
 * - DC_STATE_EN_DC9
 *
 * Signal to DMC firmware/HW the target DC power state passed in @state.
 * DMC/HW can turn off individual display clocks and power rails when entering
 * a deeper DC power state (higher in number) and turns these back when exiting
 * that state to a shallower power state (lower in number). The HW will decide
 * when to actually enter a given state on an on-demand basis, for instance
 * depending on the active state of display pipes. The state of display
 * registers backed by affected power rails are saved/restored as needed.
 *
 * Based on the above enabling a deeper DC power state is asynchronous wrt.
 * enabling it. Disabling a deeper power state is synchronous: for instance
 * setting %DC_STATE_DISABLE won't complete until all HW resources are turned
 * back on and register state is restored. This is guaranteed by the MMIO write
 * to DC_STATE_EN blocking until the state is restored.
 */
static void gen9_set_dc_state(struct drm_i915_private *dev_priv, u32 state)
{
	u32 val;
	u32 mask;

	if (drm_WARN_ON_ONCE(&dev_priv->drm,
			     state & ~dev_priv->csr.allowed_dc_mask))
		state &= dev_priv->csr.allowed_dc_mask;

	val = intel_de_read(dev_priv, DC_STATE_EN);
	mask = gen9_dc_mask(dev_priv);
	drm_dbg_kms(&dev_priv->drm, "Setting DC state from %02x to %02x\n",
		    val & mask, state);

	/* Check if DMC is ignoring our DC state requests */
	if ((val & mask) != dev_priv->csr.dc_state)
		drm_err(&dev_priv->drm, "DC state mismatch (0x%x -> 0x%x)\n",
			dev_priv->csr.dc_state, val & mask);

	val &= ~mask;
	val |= state;

	gen9_write_dc_state(dev_priv, val);

	dev_priv->csr.dc_state = val & mask;
}

static u32
sanitize_target_dc_state(struct drm_i915_private *dev_priv,
			 u32 target_dc_state)
{
	u32 states[] = {
		DC_STATE_EN_UPTO_DC6,
		DC_STATE_EN_UPTO_DC5,
		DC_STATE_EN_DC3CO,
		DC_STATE_DISABLE,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(states) - 1; i++) {
		if (target_dc_state != states[i])
			continue;

		if (dev_priv->csr.allowed_dc_mask & target_dc_state)
			break;

		target_dc_state = states[i + 1];
	}

	return target_dc_state;
}

static void tgl_enable_dc3co(struct drm_i915_private *dev_priv)
{
	drm_dbg_kms(&dev_priv->drm, "Enabling DC3CO\n");
	gen9_set_dc_state(dev_priv, DC_STATE_EN_DC3CO);
}

static void tgl_disable_dc3co(struct drm_i915_private *dev_priv)
{
	u32 val;

	drm_dbg_kms(&dev_priv->drm, "Disabling DC3CO\n");
	val = intel_de_read(dev_priv, DC_STATE_EN);
	val &= ~DC_STATE_DC3CO_STATUS;
	intel_de_write(dev_priv, DC_STATE_EN, val);
	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);
	/*
	 * Delay of 200us DC3CO Exit time B.Spec 49196
	 */
	usleep_range(200, 210);
}

static void bxt_enable_dc9(struct drm_i915_private *dev_priv)
{
	assert_can_enable_dc9(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "Enabling DC9\n");
	/*
	 * Power sequencer reset is not needed on
	 * platforms with South Display Engine on PCH,
	 * because PPS registers are always on.
	 */
	if (!HAS_PCH_SPLIT(dev_priv))
		intel_power_sequencer_reset(dev_priv);
	gen9_set_dc_state(dev_priv, DC_STATE_EN_DC9);
}

static void bxt_disable_dc9(struct drm_i915_private *dev_priv)
{
	assert_can_disable_dc9(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "Disabling DC9\n");

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	intel_pps_unlock_regs_wa(dev_priv);
}

static void assert_csr_loaded(struct drm_i915_private *dev_priv)
{
	drm_WARN_ONCE(&dev_priv->drm,
		      !intel_de_read(dev_priv, CSR_PROGRAM(0)),
		      "CSR program storage start is NULL\n");
	drm_WARN_ONCE(&dev_priv->drm, !intel_de_read(dev_priv, CSR_SSP_BASE),
		      "CSR SSP Base Not fine\n");
	drm_WARN_ONCE(&dev_priv->drm, !intel_de_read(dev_priv, CSR_HTP_SKL),
		      "CSR HTP Not fine\n");
}

static struct i915_power_well *
lookup_power_well(struct drm_i915_private *dev_priv,
		  enum i915_power_well_id power_well_id)
{
	struct i915_power_well *power_well;

	for_each_power_well(dev_priv, power_well)
		if (power_well->desc->id == power_well_id)
			return power_well;

	/*
	 * It's not feasible to add error checking code to the callers since
	 * this condition really shouldn't happen and it doesn't even make sense
	 * to abort things like display initialization sequences. Just return
	 * the first power well and hope the WARN gets reported so we can fix
	 * our driver.
	 */
	drm_WARN(&dev_priv->drm, 1,
		 "Power well %d not defined for this platform\n",
		 power_well_id);
	return &dev_priv->power_domains.power_wells[0];
}

/**
 * intel_display_power_set_target_dc_state - Set target dc state.
 * @dev_priv: i915 device
 * @state: state which needs to be set as target_dc_state.
 *
 * This function set the "DC off" power well target_dc_state,
 * based upon this target_dc_stste, "DC off" power well will
 * enable desired DC state.
 */
void intel_display_power_set_target_dc_state(struct drm_i915_private *dev_priv,
					     u32 state)
{
	struct i915_power_well *power_well;
	bool dc_off_enabled;
	struct i915_power_domains *power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);
	power_well = lookup_power_well(dev_priv, SKL_DISP_DC_OFF);

	if (drm_WARN_ON(&dev_priv->drm, !power_well))
		goto unlock;

	state = sanitize_target_dc_state(dev_priv, state);

	if (state == dev_priv->csr.target_dc_state)
		goto unlock;

	dc_off_enabled = power_well->desc->ops->is_enabled(dev_priv,
							   power_well);
	/*
	 * If DC off power well is disabled, need to enable and disable the
	 * DC off power well to effect target DC state.
	 */
	if (!dc_off_enabled)
		power_well->desc->ops->enable(dev_priv, power_well);

	dev_priv->csr.target_dc_state = state;

	if (!dc_off_enabled)
		power_well->desc->ops->disable(dev_priv, power_well);

unlock:
	mutex_unlock(&power_domains->lock);
}

static void assert_can_enable_dc5(struct drm_i915_private *dev_priv)
{
	enum i915_power_well_id high_pg;

	/* Power wells at this level and above must be disabled for DC5 entry */
	if (INTEL_GEN(dev_priv) >= 12)
		high_pg = ICL_DISP_PW_3;
	else
		high_pg = SKL_DISP_PW_2;

	drm_WARN_ONCE(&dev_priv->drm,
		      intel_display_power_well_is_enabled(dev_priv, high_pg),
		      "Power wells above platform's DC5 limit still enabled.\n");

	drm_WARN_ONCE(&dev_priv->drm,
		      (intel_de_read(dev_priv, DC_STATE_EN) &
		       DC_STATE_EN_UPTO_DC5),
		      "DC5 already programmed to be enabled.\n");
	assert_rpm_wakelock_held(&dev_priv->runtime_pm);

	assert_csr_loaded(dev_priv);
}

static void gen9_enable_dc5(struct drm_i915_private *dev_priv)
{
	assert_can_enable_dc5(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "Enabling DC5\n");

	/* Wa Display #1183: skl,kbl,cfl */
	if (IS_GEN9_BC(dev_priv))
		intel_de_write(dev_priv, GEN8_CHICKEN_DCPR_1,
			       intel_de_read(dev_priv, GEN8_CHICKEN_DCPR_1) | SKL_SELECT_ALTERNATE_DC_EXIT);

	gen9_set_dc_state(dev_priv, DC_STATE_EN_UPTO_DC5);
}

static void assert_can_enable_dc6(struct drm_i915_private *dev_priv)
{
	drm_WARN_ONCE(&dev_priv->drm,
		      intel_de_read(dev_priv, UTIL_PIN_CTL) & UTIL_PIN_ENABLE,
		      "Backlight is not disabled.\n");
	drm_WARN_ONCE(&dev_priv->drm,
		      (intel_de_read(dev_priv, DC_STATE_EN) &
		       DC_STATE_EN_UPTO_DC6),
		      "DC6 already programmed to be enabled.\n");

	assert_csr_loaded(dev_priv);
}

static void skl_enable_dc6(struct drm_i915_private *dev_priv)
{
	assert_can_enable_dc6(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "Enabling DC6\n");

	/* Wa Display #1183: skl,kbl,cfl */
	if (IS_GEN9_BC(dev_priv))
		intel_de_write(dev_priv, GEN8_CHICKEN_DCPR_1,
			       intel_de_read(dev_priv, GEN8_CHICKEN_DCPR_1) | SKL_SELECT_ALTERNATE_DC_EXIT);

	gen9_set_dc_state(dev_priv, DC_STATE_EN_UPTO_DC6);
}

static void hsw_power_well_sync_hw(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	u32 mask = HSW_PWR_WELL_CTL_REQ(pw_idx);
	u32 bios_req = intel_de_read(dev_priv, regs->bios);

	/* Take over the request bit if set by BIOS. */
	if (bios_req & mask) {
		u32 drv_req = intel_de_read(dev_priv, regs->driver);

		if (!(drv_req & mask))
			intel_de_write(dev_priv, regs->driver, drv_req | mask);
		intel_de_write(dev_priv, regs->bios, bios_req & ~mask);
	}
}

static void bxt_dpio_cmn_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	bxt_ddi_phy_init(dev_priv, power_well->desc->bxt.phy);
}

static void bxt_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	bxt_ddi_phy_uninit(dev_priv, power_well->desc->bxt.phy);
}

static bool bxt_dpio_cmn_power_well_enabled(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	return bxt_ddi_phy_is_enabled(dev_priv, power_well->desc->bxt.phy);
}

static void bxt_verify_ddi_phy_power_wells(struct drm_i915_private *dev_priv)
{
	struct i915_power_well *power_well;

	power_well = lookup_power_well(dev_priv, BXT_DISP_PW_DPIO_CMN_A);
	if (power_well->count > 0)
		bxt_ddi_phy_verify_state(dev_priv, power_well->desc->bxt.phy);

	power_well = lookup_power_well(dev_priv, VLV_DISP_PW_DPIO_CMN_BC);
	if (power_well->count > 0)
		bxt_ddi_phy_verify_state(dev_priv, power_well->desc->bxt.phy);

	if (IS_GEMINILAKE(dev_priv)) {
		power_well = lookup_power_well(dev_priv,
					       GLK_DISP_PW_DPIO_CMN_C);
		if (power_well->count > 0)
			bxt_ddi_phy_verify_state(dev_priv,
						 power_well->desc->bxt.phy);
	}
}

static bool gen9_dc_off_power_well_enabled(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	return ((intel_de_read(dev_priv, DC_STATE_EN) & DC_STATE_EN_DC3CO) == 0 &&
		(intel_de_read(dev_priv, DC_STATE_EN) & DC_STATE_EN_UPTO_DC5_DC6_MASK) == 0);
}

static void gen9_assert_dbuf_enabled(struct drm_i915_private *dev_priv)
{
	u8 hw_enabled_dbuf_slices = intel_enabled_dbuf_slices_mask(dev_priv);
	u8 enabled_dbuf_slices = dev_priv->dbuf.enabled_slices;

	drm_WARN(&dev_priv->drm,
		 hw_enabled_dbuf_slices != enabled_dbuf_slices,
		 "Unexpected DBuf power power state (0x%08x, expected 0x%08x)\n",
		 hw_enabled_dbuf_slices,
		 enabled_dbuf_slices);
}

static void gen9_disable_dc_states(struct drm_i915_private *dev_priv)
{
	struct intel_cdclk_config cdclk_config = {};

	if (dev_priv->csr.target_dc_state == DC_STATE_EN_DC3CO) {
		tgl_disable_dc3co(dev_priv);
		return;
	}

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	dev_priv->display.get_cdclk(dev_priv, &cdclk_config);
	/* Can't read out voltage_level so can't use intel_cdclk_changed() */
	drm_WARN_ON(&dev_priv->drm,
		    intel_cdclk_needs_modeset(&dev_priv->cdclk.hw,
					      &cdclk_config));

	gen9_assert_dbuf_enabled(dev_priv);

	if (IS_GEN9_LP(dev_priv))
		bxt_verify_ddi_phy_power_wells(dev_priv);

	if (INTEL_GEN(dev_priv) >= 11)
		/*
		 * DMC retains HW context only for port A, the other combo
		 * PHY's HW context for port B is lost after DC transitions,
		 * so we need to restore it manually.
		 */
		intel_combo_phy_init(dev_priv);
}

static void gen9_dc_off_power_well_enable(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	gen9_disable_dc_states(dev_priv);
}

static void gen9_dc_off_power_well_disable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	if (!dev_priv->csr.dmc_payload)
		return;

	switch (dev_priv->csr.target_dc_state) {
	case DC_STATE_EN_DC3CO:
		tgl_enable_dc3co(dev_priv);
		break;
	case DC_STATE_EN_UPTO_DC6:
		skl_enable_dc6(dev_priv);
		break;
	case DC_STATE_EN_UPTO_DC5:
		gen9_enable_dc5(dev_priv);
		break;
	}
}

static void i9xx_power_well_sync_hw_noop(struct drm_i915_private *dev_priv,
					 struct i915_power_well *power_well)
{
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

static void i830_pipes_power_well_enable(struct drm_i915_private *dev_priv,
					 struct i915_power_well *power_well)
{
	if ((intel_de_read(dev_priv, PIPECONF(PIPE_A)) & PIPECONF_ENABLE) == 0)
		i830_enable_pipe(dev_priv, PIPE_A);
	if ((intel_de_read(dev_priv, PIPECONF(PIPE_B)) & PIPECONF_ENABLE) == 0)
		i830_enable_pipe(dev_priv, PIPE_B);
}

static void i830_pipes_power_well_disable(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	i830_disable_pipe(dev_priv, PIPE_B);
	i830_disable_pipe(dev_priv, PIPE_A);
}

static bool i830_pipes_power_well_enabled(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	return intel_de_read(dev_priv, PIPECONF(PIPE_A)) & PIPECONF_ENABLE &&
		intel_de_read(dev_priv, PIPECONF(PIPE_B)) & PIPECONF_ENABLE;
}

static void i830_pipes_power_well_sync_hw(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	if (power_well->count > 0)
		i830_pipes_power_well_enable(dev_priv, power_well);
	else
		i830_pipes_power_well_disable(dev_priv, power_well);
}

static void vlv_set_power_well(struct drm_i915_private *dev_priv,
			       struct i915_power_well *power_well, bool enable)
{
	int pw_idx = power_well->desc->vlv.idx;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(pw_idx);
	state = enable ? PUNIT_PWRGT_PWR_ON(pw_idx) :
			 PUNIT_PWRGT_PWR_GATE(pw_idx);

	vlv_punit_get(dev_priv);

#define COND \
	((vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL);
	ctrl &= ~mask;
	ctrl |= state;
	vlv_punit_write(dev_priv, PUNIT_REG_PWRGT_CTRL, ctrl);

	if (wait_for(COND, 100))
		drm_err(&dev_priv->drm,
			"timeout setting power well state %08x (%08x)\n",
			state,
			vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL));

#undef COND

out:
	vlv_punit_put(dev_priv);
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
	int pw_idx = power_well->desc->vlv.idx;
	bool enabled = false;
	u32 mask;
	u32 state;
	u32 ctrl;

	mask = PUNIT_PWRGT_MASK(pw_idx);
	ctrl = PUNIT_PWRGT_PWR_ON(pw_idx);

	vlv_punit_get(dev_priv);

	state = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask;
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	drm_WARN_ON(&dev_priv->drm, state != PUNIT_PWRGT_PWR_ON(pw_idx) &&
		    state != PUNIT_PWRGT_PWR_GATE(pw_idx));
	if (state == ctrl)
		enabled = true;

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL) & mask;
	drm_WARN_ON(&dev_priv->drm, ctrl != state);

	vlv_punit_put(dev_priv);

	return enabled;
}

static void vlv_init_display_clock_gating(struct drm_i915_private *dev_priv)
{
	u32 val;

	/*
	 * On driver load, a pipe may be active and driving a DSI display.
	 * Preserve DPOUNIT_CLOCK_GATE_DISABLE to avoid the pipe getting stuck
	 * (and never recovering) in this case. intel_dsi_post_disable() will
	 * clear it when we turn off the display.
	 */
	val = intel_de_read(dev_priv, DSPCLK_GATE_D);
	val &= DPOUNIT_CLOCK_GATE_DISABLE;
	val |= VRHUNIT_CLOCK_GATE_DISABLE;
	intel_de_write(dev_priv, DSPCLK_GATE_D, val);

	/*
	 * Disable trickle feed and enable pnd deadline calculation
	 */
	intel_de_write(dev_priv, MI_ARB_VLV,
		       MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE);
	intel_de_write(dev_priv, CBR1_VLV, 0);

	drm_WARN_ON(&dev_priv->drm, RUNTIME_INFO(dev_priv)->rawclk_freq == 0);
	intel_de_write(dev_priv, RAWCLK_FREQ_VLV,
		       DIV_ROUND_CLOSEST(RUNTIME_INFO(dev_priv)->rawclk_freq,
					 1000));
}

static void vlv_display_power_well_init(struct drm_i915_private *dev_priv)
{
	struct intel_encoder *encoder;
	enum pipe pipe;

	/*
	 * Enable the CRI clock source so we can get at the
	 * display and the reference clock for VGA
	 * hotplug / manual detection. Supposedly DSI also
	 * needs the ref clock up and running.
	 *
	 * CHV DPLL B/C have some issues if VGA mode is enabled.
	 */
	for_each_pipe(dev_priv, pipe) {
		u32 val = intel_de_read(dev_priv, DPLL(pipe));

		val |= DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
		if (pipe != PIPE_A)
			val |= DPLL_INTEGRATED_CRI_CLK_VLV;

		intel_de_write(dev_priv, DPLL(pipe), val);
	}

	vlv_init_display_clock_gating(dev_priv);

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
	intel_hpd_poll_disable(dev_priv);

	/* Re-enable the ADPA, if we have one */
	for_each_intel_encoder(&dev_priv->drm, encoder) {
		if (encoder->type == INTEL_OUTPUT_ANALOG)
			intel_crt_reset(&encoder->base);
	}

	intel_vga_redisable_power_on(dev_priv);

	intel_pps_unlock_regs_wa(dev_priv);
}

static void vlv_display_power_well_deinit(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->irq_lock);
	valleyview_disable_display_irqs(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	/* make sure we're done processing display irqs */
	intel_synchronize_irq(dev_priv);

	intel_power_sequencer_reset(dev_priv);

	/* Prevent us from re-enabling polling on accident in late suspend */
	if (!dev_priv->drm.dev->power.is_suspended)
		intel_hpd_poll_enable(dev_priv);
}

static void vlv_display_power_well_enable(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	vlv_set_power_well(dev_priv, power_well, true);

	vlv_display_power_well_init(dev_priv);
}

static void vlv_display_power_well_disable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	vlv_display_power_well_deinit(dev_priv);

	vlv_set_power_well(dev_priv, power_well, false);
}

static void vlv_dpio_cmn_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	/* since ref/cri clock was enabled */
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
	intel_de_write(dev_priv, DPIO_CTL,
		       intel_de_read(dev_priv, DPIO_CTL) | DPIO_CMNRST);
}

static void vlv_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		assert_pll_disabled(dev_priv, pipe);

	/* Assert common reset */
	intel_de_write(dev_priv, DPIO_CTL,
		       intel_de_read(dev_priv, DPIO_CTL) & ~DPIO_CMNRST);

	vlv_set_power_well(dev_priv, power_well, false);
}

#define POWER_DOMAIN_MASK (GENMASK_ULL(POWER_DOMAIN_NUM - 1, 0))

#define BITS_SET(val, bits) (((val) & (bits)) == (bits))

static void assert_chv_phy_status(struct drm_i915_private *dev_priv)
{
	struct i915_power_well *cmn_bc =
		lookup_power_well(dev_priv, VLV_DISP_PW_DPIO_CMN_BC);
	struct i915_power_well *cmn_d =
		lookup_power_well(dev_priv, CHV_DISP_PW_DPIO_CMN_D);
	u32 phy_control = dev_priv->chv_phy_control;
	u32 phy_status = 0;
	u32 phy_status_mask = 0xffffffff;

	/*
	 * The BIOS can leave the PHY is some weird state
	 * where it doesn't fully power down some parts.
	 * Disable the asserts until the PHY has been fully
	 * reset (ie. the power well has been disabled at
	 * least once).
	 */
	if (!dev_priv->chv_phy_assert[DPIO_PHY0])
		phy_status_mask &= ~(PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 1) |
				     PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH1) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 1));

	if (!dev_priv->chv_phy_assert[DPIO_PHY1])
		phy_status_mask &= ~(PHY_STATUS_CMN_LDO(DPIO_PHY1, DPIO_CH0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 0) |
				     PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 1));

	if (cmn_bc->desc->ops->is_enabled(dev_priv, cmn_bc)) {
		phy_status |= PHY_POWERGOOD(DPIO_PHY0);

		/* this assumes override is only used to enable lanes */
		if ((phy_control & PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY0, DPIO_CH0)) == 0)
			phy_control |= PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH0);

		if ((phy_control & PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY0, DPIO_CH1)) == 0)
			phy_control |= PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH1);

		/* CL1 is on whenever anything is on in either channel */
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH0) |
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH1)))
			phy_status |= PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH0);

		/*
		 * The DPLLB check accounts for the pipe B + port A usage
		 * with CL2 powered up but all the lanes in the second channel
		 * powered down.
		 */
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY0, DPIO_CH1)) &&
		    (intel_de_read(dev_priv, DPLL(PIPE_B)) & DPLL_VCO_ENABLE) == 0)
			phy_status |= PHY_STATUS_CMN_LDO(DPIO_PHY0, DPIO_CH1);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0x3, DPIO_PHY0, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 0);
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xc, DPIO_PHY0, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH0, 1);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0x3, DPIO_PHY0, DPIO_CH1)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 0);
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xc, DPIO_PHY0, DPIO_CH1)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY0, DPIO_CH1, 1);
	}

	if (cmn_d->desc->ops->is_enabled(dev_priv, cmn_d)) {
		phy_status |= PHY_POWERGOOD(DPIO_PHY1);

		/* this assumes override is only used to enable lanes */
		if ((phy_control & PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY1, DPIO_CH0)) == 0)
			phy_control |= PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY1, DPIO_CH0);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xf, DPIO_PHY1, DPIO_CH0)))
			phy_status |= PHY_STATUS_CMN_LDO(DPIO_PHY1, DPIO_CH0);

		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0x3, DPIO_PHY1, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 0);
		if (BITS_SET(phy_control,
			     PHY_CH_POWER_DOWN_OVRD(0xc, DPIO_PHY1, DPIO_CH0)))
			phy_status |= PHY_STATUS_SPLINE_LDO(DPIO_PHY1, DPIO_CH0, 1);
	}

	phy_status &= phy_status_mask;

	/*
	 * The PHY may be busy with some initial calibration and whatnot,
	 * so the power state can take a while to actually change.
	 */
	if (intel_de_wait_for_register(dev_priv, DISPLAY_PHY_STATUS,
				       phy_status_mask, phy_status, 10))
		drm_err(&dev_priv->drm,
			"Unexpected PHY_STATUS 0x%08x, expected 0x%08x (PHY_CONTROL=0x%08x)\n",
			intel_de_read(dev_priv, DISPLAY_PHY_STATUS) & phy_status_mask,
			phy_status, dev_priv->chv_phy_control);
}

#undef BITS_SET

static void chv_dpio_cmn_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	enum dpio_phy phy;
	enum pipe pipe;
	u32 tmp;

	drm_WARN_ON_ONCE(&dev_priv->drm,
			 power_well->desc->id != VLV_DISP_PW_DPIO_CMN_BC &&
			 power_well->desc->id != CHV_DISP_PW_DPIO_CMN_D);

	if (power_well->desc->id == VLV_DISP_PW_DPIO_CMN_BC) {
		pipe = PIPE_A;
		phy = DPIO_PHY0;
	} else {
		pipe = PIPE_C;
		phy = DPIO_PHY1;
	}

	/* since ref/cri clock was enabled */
	udelay(1); /* >10ns for cmnreset, >0ns for sidereset */
	vlv_set_power_well(dev_priv, power_well, true);

	/* Poll for phypwrgood signal */
	if (intel_de_wait_for_set(dev_priv, DISPLAY_PHY_STATUS,
				  PHY_POWERGOOD(phy), 1))
		drm_err(&dev_priv->drm, "Display PHY %d is not power up\n",
			phy);

	vlv_dpio_get(dev_priv);

	/* Enable dynamic power down */
	tmp = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW28);
	tmp |= DPIO_DYNPWRDOWNEN_CH0 | DPIO_CL1POWERDOWNEN |
		DPIO_SUS_CLK_CONFIG_GATE_CLKREQ;
	vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW28, tmp);

	if (power_well->desc->id == VLV_DISP_PW_DPIO_CMN_BC) {
		tmp = vlv_dpio_read(dev_priv, pipe, _CHV_CMN_DW6_CH1);
		tmp |= DPIO_DYNPWRDOWNEN_CH1;
		vlv_dpio_write(dev_priv, pipe, _CHV_CMN_DW6_CH1, tmp);
	} else {
		/*
		 * Force the non-existing CL2 off. BXT does this
		 * too, so maybe it saves some power even though
		 * CL2 doesn't exist?
		 */
		tmp = vlv_dpio_read(dev_priv, pipe, CHV_CMN_DW30);
		tmp |= DPIO_CL2_LDOFUSE_PWRENB;
		vlv_dpio_write(dev_priv, pipe, CHV_CMN_DW30, tmp);
	}

	vlv_dpio_put(dev_priv);

	dev_priv->chv_phy_control |= PHY_COM_LANE_RESET_DEASSERT(phy);
	intel_de_write(dev_priv, DISPLAY_PHY_CONTROL,
		       dev_priv->chv_phy_control);

	drm_dbg_kms(&dev_priv->drm,
		    "Enabled DPIO PHY%d (PHY_CONTROL=0x%08x)\n",
		    phy, dev_priv->chv_phy_control);

	assert_chv_phy_status(dev_priv);
}

static void chv_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	enum dpio_phy phy;

	drm_WARN_ON_ONCE(&dev_priv->drm,
			 power_well->desc->id != VLV_DISP_PW_DPIO_CMN_BC &&
			 power_well->desc->id != CHV_DISP_PW_DPIO_CMN_D);

	if (power_well->desc->id == VLV_DISP_PW_DPIO_CMN_BC) {
		phy = DPIO_PHY0;
		assert_pll_disabled(dev_priv, PIPE_A);
		assert_pll_disabled(dev_priv, PIPE_B);
	} else {
		phy = DPIO_PHY1;
		assert_pll_disabled(dev_priv, PIPE_C);
	}

	dev_priv->chv_phy_control &= ~PHY_COM_LANE_RESET_DEASSERT(phy);
	intel_de_write(dev_priv, DISPLAY_PHY_CONTROL,
		       dev_priv->chv_phy_control);

	vlv_set_power_well(dev_priv, power_well, false);

	drm_dbg_kms(&dev_priv->drm,
		    "Disabled DPIO PHY%d (PHY_CONTROL=0x%08x)\n",
		    phy, dev_priv->chv_phy_control);

	/* PHY is fully reset now, so we can enable the PHY state asserts */
	dev_priv->chv_phy_assert[phy] = true;

	assert_chv_phy_status(dev_priv);
}

static void assert_chv_phy_powergate(struct drm_i915_private *dev_priv, enum dpio_phy phy,
				     enum dpio_channel ch, bool override, unsigned int mask)
{
	enum pipe pipe = phy == DPIO_PHY0 ? PIPE_A : PIPE_C;
	u32 reg, val, expected, actual;

	/*
	 * The BIOS can leave the PHY is some weird state
	 * where it doesn't fully power down some parts.
	 * Disable the asserts until the PHY has been fully
	 * reset (ie. the power well has been disabled at
	 * least once).
	 */
	if (!dev_priv->chv_phy_assert[phy])
		return;

	if (ch == DPIO_CH0)
		reg = _CHV_CMN_DW0_CH0;
	else
		reg = _CHV_CMN_DW6_CH1;

	vlv_dpio_get(dev_priv);
	val = vlv_dpio_read(dev_priv, pipe, reg);
	vlv_dpio_put(dev_priv);

	/*
	 * This assumes !override is only used when the port is disabled.
	 * All lanes should power down even without the override when
	 * the port is disabled.
	 */
	if (!override || mask == 0xf) {
		expected = DPIO_ALLDL_POWERDOWN | DPIO_ANYDL_POWERDOWN;
		/*
		 * If CH1 common lane is not active anymore
		 * (eg. for pipe B DPLL) the entire channel will
		 * shut down, which causes the common lane registers
		 * to read as 0. That means we can't actually check
		 * the lane power down status bits, but as the entire
		 * register reads as 0 it's a good indication that the
		 * channel is indeed entirely powered down.
		 */
		if (ch == DPIO_CH1 && val == 0)
			expected = 0;
	} else if (mask != 0x0) {
		expected = DPIO_ANYDL_POWERDOWN;
	} else {
		expected = 0;
	}

	if (ch == DPIO_CH0)
		actual = val >> DPIO_ANYDL_POWERDOWN_SHIFT_CH0;
	else
		actual = val >> DPIO_ANYDL_POWERDOWN_SHIFT_CH1;
	actual &= DPIO_ALLDL_POWERDOWN | DPIO_ANYDL_POWERDOWN;

	drm_WARN(&dev_priv->drm, actual != expected,
		 "Unexpected DPIO lane power down: all %d, any %d. Expected: all %d, any %d. (0x%x = 0x%08x)\n",
		 !!(actual & DPIO_ALLDL_POWERDOWN),
		 !!(actual & DPIO_ANYDL_POWERDOWN),
		 !!(expected & DPIO_ALLDL_POWERDOWN),
		 !!(expected & DPIO_ANYDL_POWERDOWN),
		 reg, val);
}

bool chv_phy_powergate_ch(struct drm_i915_private *dev_priv, enum dpio_phy phy,
			  enum dpio_channel ch, bool override)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	bool was_override;

	mutex_lock(&power_domains->lock);

	was_override = dev_priv->chv_phy_control & PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);

	if (override == was_override)
		goto out;

	if (override)
		dev_priv->chv_phy_control |= PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);
	else
		dev_priv->chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);

	intel_de_write(dev_priv, DISPLAY_PHY_CONTROL,
		       dev_priv->chv_phy_control);

	drm_dbg_kms(&dev_priv->drm,
		    "Power gating DPIO PHY%d CH%d (DPIO_PHY_CONTROL=0x%08x)\n",
		    phy, ch, dev_priv->chv_phy_control);

	assert_chv_phy_status(dev_priv);

out:
	mutex_unlock(&power_domains->lock);

	return was_override;
}

void chv_phy_powergate_lanes(struct intel_encoder *encoder,
			     bool override, unsigned int mask)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	enum dpio_phy phy = vlv_dig_port_to_phy(enc_to_dig_port(encoder));
	enum dpio_channel ch = vlv_dig_port_to_channel(enc_to_dig_port(encoder));

	mutex_lock(&power_domains->lock);

	dev_priv->chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD(0xf, phy, ch);
	dev_priv->chv_phy_control |= PHY_CH_POWER_DOWN_OVRD(mask, phy, ch);

	if (override)
		dev_priv->chv_phy_control |= PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);
	else
		dev_priv->chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);

	intel_de_write(dev_priv, DISPLAY_PHY_CONTROL,
		       dev_priv->chv_phy_control);

	drm_dbg_kms(&dev_priv->drm,
		    "Power gating DPIO PHY%d CH%d lanes 0x%x (PHY_CONTROL=0x%08x)\n",
		    phy, ch, mask, dev_priv->chv_phy_control);

	assert_chv_phy_status(dev_priv);

	assert_chv_phy_powergate(dev_priv, phy, ch, override, mask);

	mutex_unlock(&power_domains->lock);
}

static bool chv_pipe_power_well_enabled(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well)
{
	enum pipe pipe = PIPE_A;
	bool enabled;
	u32 state, ctrl;

	vlv_punit_get(dev_priv);

	state = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM) & DP_SSS_MASK(pipe);
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	drm_WARN_ON(&dev_priv->drm, state != DP_SSS_PWR_ON(pipe) &&
		    state != DP_SSS_PWR_GATE(pipe));
	enabled = state == DP_SSS_PWR_ON(pipe);

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM) & DP_SSC_MASK(pipe);
	drm_WARN_ON(&dev_priv->drm, ctrl << 16 != state);

	vlv_punit_put(dev_priv);

	return enabled;
}

static void chv_set_pipe_power_well(struct drm_i915_private *dev_priv,
				    struct i915_power_well *power_well,
				    bool enable)
{
	enum pipe pipe = PIPE_A;
	u32 state;
	u32 ctrl;

	state = enable ? DP_SSS_PWR_ON(pipe) : DP_SSS_PWR_GATE(pipe);

	vlv_punit_get(dev_priv);

#define COND \
	((vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM) & DP_SSS_MASK(pipe)) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM);
	ctrl &= ~DP_SSC_MASK(pipe);
	ctrl |= enable ? DP_SSC_PWR_ON(pipe) : DP_SSC_PWR_GATE(pipe);
	vlv_punit_write(dev_priv, PUNIT_REG_DSPSSPM, ctrl);

	if (wait_for(COND, 100))
		drm_err(&dev_priv->drm,
			"timeout setting power well state %08x (%08x)\n",
			state,
			vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM));

#undef COND

out:
	vlv_punit_put(dev_priv);
}

static void chv_pipe_power_well_sync_hw(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well)
{
	intel_de_write(dev_priv, DISPLAY_PHY_CONTROL,
		       dev_priv->chv_phy_control);
}

static void chv_pipe_power_well_enable(struct drm_i915_private *dev_priv,
				       struct i915_power_well *power_well)
{
	chv_set_pipe_power_well(dev_priv, power_well, true);

	vlv_display_power_well_init(dev_priv);
}

static void chv_pipe_power_well_disable(struct drm_i915_private *dev_priv,
					struct i915_power_well *power_well)
{
	vlv_display_power_well_deinit(dev_priv);

	chv_set_pipe_power_well(dev_priv, power_well, false);
}

static u64 __async_put_domains_mask(struct i915_power_domains *power_domains)
{
	return power_domains->async_put_domains[0] |
	       power_domains->async_put_domains[1];
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)

static bool
assert_async_put_domain_masks_disjoint(struct i915_power_domains *power_domains)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);
	return !drm_WARN_ON(&i915->drm, power_domains->async_put_domains[0] &
			    power_domains->async_put_domains[1]);
}

static bool
__async_put_domains_state_ok(struct i915_power_domains *power_domains)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);
	enum intel_display_power_domain domain;
	bool err = false;

	err |= !assert_async_put_domain_masks_disjoint(power_domains);
	err |= drm_WARN_ON(&i915->drm, !!power_domains->async_put_wakeref !=
			   !!__async_put_domains_mask(power_domains));

	for_each_power_domain(domain, __async_put_domains_mask(power_domains))
		err |= drm_WARN_ON(&i915->drm,
				   power_domains->domain_use_count[domain] != 1);

	return !err;
}

static void print_power_domains(struct i915_power_domains *power_domains,
				const char *prefix, u64 mask)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);
	enum intel_display_power_domain domain;

	drm_dbg(&i915->drm, "%s (%lu):\n", prefix, hweight64(mask));
	for_each_power_domain(domain, mask)
		drm_dbg(&i915->drm, "%s use_count %d\n",
			intel_display_power_domain_str(domain),
			power_domains->domain_use_count[domain]);
}

static void
print_async_put_domains_state(struct i915_power_domains *power_domains)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);

	drm_dbg(&i915->drm, "async_put_wakeref %u\n",
		power_domains->async_put_wakeref);

	print_power_domains(power_domains, "async_put_domains[0]",
			    power_domains->async_put_domains[0]);
	print_power_domains(power_domains, "async_put_domains[1]",
			    power_domains->async_put_domains[1]);
}

static void
verify_async_put_domains_state(struct i915_power_domains *power_domains)
{
	if (!__async_put_domains_state_ok(power_domains))
		print_async_put_domains_state(power_domains);
}

#else

static void
assert_async_put_domain_masks_disjoint(struct i915_power_domains *power_domains)
{
}

static void
verify_async_put_domains_state(struct i915_power_domains *power_domains)
{
}

#endif /* CONFIG_DRM_I915_DEBUG_RUNTIME_PM */

static u64 async_put_domains_mask(struct i915_power_domains *power_domains)
{
	assert_async_put_domain_masks_disjoint(power_domains);

	return __async_put_domains_mask(power_domains);
}

static void
async_put_domains_clear_domain(struct i915_power_domains *power_domains,
			       enum intel_display_power_domain domain)
{
	assert_async_put_domain_masks_disjoint(power_domains);

	power_domains->async_put_domains[0] &= ~BIT_ULL(domain);
	power_domains->async_put_domains[1] &= ~BIT_ULL(domain);
}

static bool
intel_display_power_grab_async_put_ref(struct drm_i915_private *dev_priv,
				       enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	bool ret = false;

	if (!(async_put_domains_mask(power_domains) & BIT_ULL(domain)))
		goto out_verify;

	async_put_domains_clear_domain(power_domains, domain);

	ret = true;

	if (async_put_domains_mask(power_domains))
		goto out_verify;

	cancel_delayed_work(&power_domains->async_put_work);
	intel_runtime_pm_put_raw(&dev_priv->runtime_pm,
				 fetch_and_zero(&power_domains->async_put_wakeref));
out_verify:
	verify_async_put_domains_state(power_domains);

	return ret;
}

static void
__intel_display_power_get_domain(struct drm_i915_private *dev_priv,
				 enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;

	if (intel_display_power_grab_async_put_ref(dev_priv, domain))
		return;

	for_each_power_domain_well(dev_priv, power_well, BIT_ULL(domain))
		intel_power_well_get(dev_priv, power_well);

	power_domains->domain_use_count[domain]++;
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
intel_wakeref_t intel_display_power_get(struct drm_i915_private *dev_priv,
					enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	intel_wakeref_t wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	mutex_lock(&power_domains->lock);
	__intel_display_power_get_domain(dev_priv, domain);
	mutex_unlock(&power_domains->lock);

	return wakeref;
}

/**
 * intel_display_power_get_if_enabled - grab a reference for an enabled display power domain
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
intel_wakeref_t
intel_display_power_get_if_enabled(struct drm_i915_private *dev_priv,
				   enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	intel_wakeref_t wakeref;
	bool is_enabled;

	wakeref = intel_runtime_pm_get_if_in_use(&dev_priv->runtime_pm);
	if (!wakeref)
		return false;

	mutex_lock(&power_domains->lock);

	if (__intel_display_power_is_enabled(dev_priv, domain)) {
		__intel_display_power_get_domain(dev_priv, domain);
		is_enabled = true;
	} else {
		is_enabled = false;
	}

	mutex_unlock(&power_domains->lock);

	if (!is_enabled) {
		intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
		wakeref = 0;
	}

	return wakeref;
}

static void
__intel_display_power_put_domain(struct drm_i915_private *dev_priv,
				 enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;
	const char *name = intel_display_power_domain_str(domain);

	power_domains = &dev_priv->power_domains;

	drm_WARN(&dev_priv->drm, !power_domains->domain_use_count[domain],
		 "Use count on domain %s is already zero\n",
		 name);
	drm_WARN(&dev_priv->drm,
		 async_put_domains_mask(power_domains) & BIT_ULL(domain),
		 "Async disabling of domain %s is pending\n",
		 name);

	power_domains->domain_use_count[domain]--;

	for_each_power_domain_well_reverse(dev_priv, power_well, BIT_ULL(domain))
		intel_power_well_put(dev_priv, power_well);
}

static void __intel_display_power_put(struct drm_i915_private *dev_priv,
				      enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);
	__intel_display_power_put_domain(dev_priv, domain);
	mutex_unlock(&power_domains->lock);
}

/**
 * intel_display_power_put_unchecked - release an unchecked power domain reference
 * @dev_priv: i915 device instance
 * @domain: power domain to reference
 *
 * This function drops the power domain reference obtained by
 * intel_display_power_get() and might power down the corresponding hardware
 * block right away if this is the last reference.
 *
 * This function exists only for historical reasons and should be avoided in
 * new code, as the correctness of its use cannot be checked. Always use
 * intel_display_power_put() instead.
 */
void intel_display_power_put_unchecked(struct drm_i915_private *dev_priv,
				       enum intel_display_power_domain domain)
{
	__intel_display_power_put(dev_priv, domain);
	intel_runtime_pm_put_unchecked(&dev_priv->runtime_pm);
}

static void
queue_async_put_domains_work(struct i915_power_domains *power_domains,
			     intel_wakeref_t wakeref)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);
	drm_WARN_ON(&i915->drm, power_domains->async_put_wakeref);
	power_domains->async_put_wakeref = wakeref;
	drm_WARN_ON(&i915->drm, !queue_delayed_work(system_unbound_wq,
						    &power_domains->async_put_work,
						    msecs_to_jiffies(100)));
}

static void
release_async_put_domains(struct i915_power_domains *power_domains, u64 mask)
{
	struct drm_i915_private *dev_priv =
		container_of(power_domains, struct drm_i915_private,
			     power_domains);
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	enum intel_display_power_domain domain;
	intel_wakeref_t wakeref;

	/*
	 * The caller must hold already raw wakeref, upgrade that to a proper
	 * wakeref to make the state checker happy about the HW access during
	 * power well disabling.
	 */
	assert_rpm_raw_wakeref_held(rpm);
	wakeref = intel_runtime_pm_get(rpm);

	for_each_power_domain(domain, mask) {
		/* Clear before put, so put's sanity check is happy. */
		async_put_domains_clear_domain(power_domains, domain);
		__intel_display_power_put_domain(dev_priv, domain);
	}

	intel_runtime_pm_put(rpm, wakeref);
}

static void
intel_display_power_put_async_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, struct drm_i915_private,
			     power_domains.async_put_work.work);
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct intel_runtime_pm *rpm = &dev_priv->runtime_pm;
	intel_wakeref_t new_work_wakeref = intel_runtime_pm_get_raw(rpm);
	intel_wakeref_t old_work_wakeref = 0;

	mutex_lock(&power_domains->lock);

	/*
	 * Bail out if all the domain refs pending to be released were grabbed
	 * by subsequent gets or a flush_work.
	 */
	old_work_wakeref = fetch_and_zero(&power_domains->async_put_wakeref);
	if (!old_work_wakeref)
		goto out_verify;

	release_async_put_domains(power_domains,
				  power_domains->async_put_domains[0]);

	/* Requeue the work if more domains were async put meanwhile. */
	if (power_domains->async_put_domains[1]) {
		power_domains->async_put_domains[0] =
			fetch_and_zero(&power_domains->async_put_domains[1]);
		queue_async_put_domains_work(power_domains,
					     fetch_and_zero(&new_work_wakeref));
	}

out_verify:
	verify_async_put_domains_state(power_domains);

	mutex_unlock(&power_domains->lock);

	if (old_work_wakeref)
		intel_runtime_pm_put_raw(rpm, old_work_wakeref);
	if (new_work_wakeref)
		intel_runtime_pm_put_raw(rpm, new_work_wakeref);
}

/**
 * intel_display_power_put_async - release a power domain reference asynchronously
 * @i915: i915 device instance
 * @domain: power domain to reference
 * @wakeref: wakeref acquired for the reference that is being released
 *
 * This function drops the power domain reference obtained by
 * intel_display_power_get*() and schedules a work to power down the
 * corresponding hardware block if this is the last reference.
 */
void __intel_display_power_put_async(struct drm_i915_private *i915,
				     enum intel_display_power_domain domain,
				     intel_wakeref_t wakeref)
{
	struct i915_power_domains *power_domains = &i915->power_domains;
	struct intel_runtime_pm *rpm = &i915->runtime_pm;
	intel_wakeref_t work_wakeref = intel_runtime_pm_get_raw(rpm);

	mutex_lock(&power_domains->lock);

	if (power_domains->domain_use_count[domain] > 1) {
		__intel_display_power_put_domain(i915, domain);

		goto out_verify;
	}

	drm_WARN_ON(&i915->drm, power_domains->domain_use_count[domain] != 1);

	/* Let a pending work requeue itself or queue a new one. */
	if (power_domains->async_put_wakeref) {
		power_domains->async_put_domains[1] |= BIT_ULL(domain);
	} else {
		power_domains->async_put_domains[0] |= BIT_ULL(domain);
		queue_async_put_domains_work(power_domains,
					     fetch_and_zero(&work_wakeref));
	}

out_verify:
	verify_async_put_domains_state(power_domains);

	mutex_unlock(&power_domains->lock);

	if (work_wakeref)
		intel_runtime_pm_put_raw(rpm, work_wakeref);

	intel_runtime_pm_put(rpm, wakeref);
}

/**
 * intel_display_power_flush_work - flushes the async display power disabling work
 * @i915: i915 device instance
 *
 * Flushes any pending work that was scheduled by a preceding
 * intel_display_power_put_async() call, completing the disabling of the
 * corresponding power domains.
 *
 * Note that the work handler function may still be running after this
 * function returns; to ensure that the work handler isn't running use
 * intel_display_power_flush_work_sync() instead.
 */
void intel_display_power_flush_work(struct drm_i915_private *i915)
{
	struct i915_power_domains *power_domains = &i915->power_domains;
	intel_wakeref_t work_wakeref;

	mutex_lock(&power_domains->lock);

	work_wakeref = fetch_and_zero(&power_domains->async_put_wakeref);
	if (!work_wakeref)
		goto out_verify;

	release_async_put_domains(power_domains,
				  async_put_domains_mask(power_domains));
	cancel_delayed_work(&power_domains->async_put_work);

out_verify:
	verify_async_put_domains_state(power_domains);

	mutex_unlock(&power_domains->lock);

	if (work_wakeref)
		intel_runtime_pm_put_raw(&i915->runtime_pm, work_wakeref);
}

/**
 * intel_display_power_flush_work_sync - flushes and syncs the async display power disabling work
 * @i915: i915 device instance
 *
 * Like intel_display_power_flush_work(), but also ensure that the work
 * handler function is not running any more when this function returns.
 */
static void
intel_display_power_flush_work_sync(struct drm_i915_private *i915)
{
	struct i915_power_domains *power_domains = &i915->power_domains;

	intel_display_power_flush_work(i915);
	cancel_delayed_work_sync(&power_domains->async_put_work);

	verify_async_put_domains_state(power_domains);

	drm_WARN_ON(&i915->drm, power_domains->async_put_wakeref);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
/**
 * intel_display_power_put - release a power domain reference
 * @dev_priv: i915 device instance
 * @domain: power domain to reference
 * @wakeref: wakeref acquired for the reference that is being released
 *
 * This function drops the power domain reference obtained by
 * intel_display_power_get() and might power down the corresponding hardware
 * block right away if this is the last reference.
 */
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain,
			     intel_wakeref_t wakeref)
{
	__intel_display_power_put(dev_priv, domain);
	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
}
#endif

#define I830_PIPES_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PIPE_A) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_A_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |	\
	BIT_ULL(POWER_DOMAIN_INIT))

#define VLV_DISPLAY_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_DISPLAY_CORE) |	\
	BIT_ULL(POWER_DOMAIN_PIPE_A) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_A_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DSI) |		\
	BIT_ULL(POWER_DOMAIN_PORT_CRT) |		\
	BIT_ULL(POWER_DOMAIN_VGA) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |		\
	BIT_ULL(POWER_DOMAIN_AUX_C) |		\
	BIT_ULL(POWER_DOMAIN_GMBUS) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define VLV_DPIO_CMN_BC_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_CRT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |		\
	BIT_ULL(POWER_DOMAIN_AUX_C) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_B) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_B) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_C) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_C) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define CHV_DISPLAY_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_DISPLAY_CORE) |	\
	BIT_ULL(POWER_DOMAIN_PIPE_A) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_A_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DSI) |		\
	BIT_ULL(POWER_DOMAIN_VGA) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |		\
	BIT_ULL(POWER_DOMAIN_AUX_C) |		\
	BIT_ULL(POWER_DOMAIN_AUX_D) |		\
	BIT_ULL(POWER_DOMAIN_GMBUS) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define CHV_DPIO_CMN_BC_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_B) |		\
	BIT_ULL(POWER_DOMAIN_AUX_C) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define CHV_DPIO_CMN_D_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_D) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define HSW_DISPLAY_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_PIPE_A_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_CRT) | /* DDI E */	\
	BIT_ULL(POWER_DOMAIN_VGA) |				\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define BDW_DISPLAY_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_CRT) | /* DDI E */	\
	BIT_ULL(POWER_DOMAIN_VGA) |				\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define SKL_DISPLAY_POWERWELL_2_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |                       \
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_AUX_D) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_VGA) |				\
	BIT_ULL(POWER_DOMAIN_INIT))
#define SKL_DISPLAY_DDI_IO_A_E_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_A_IO) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define SKL_DISPLAY_DDI_IO_B_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define SKL_DISPLAY_DDI_IO_C_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define SKL_DISPLAY_DDI_IO_D_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define SKL_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	SKL_DISPLAY_POWERWELL_2_POWER_DOMAINS |		\
	BIT_ULL(POWER_DOMAIN_GT_IRQ) |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define BXT_DISPLAY_POWERWELL_2_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_VGA) |				\
	BIT_ULL(POWER_DOMAIN_INIT))
#define BXT_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	BXT_DISPLAY_POWERWELL_2_POWER_DOMAINS |		\
	BIT_ULL(POWER_DOMAIN_GT_IRQ) |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_GMBUS) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define BXT_DPIO_CMN_A_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_A_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define BXT_DPIO_CMN_BC_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define GLK_DISPLAY_POWERWELL_2_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |                       \
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_VGA) |				\
	BIT_ULL(POWER_DOMAIN_INIT))
#define GLK_DISPLAY_DDI_IO_A_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_A_IO))
#define GLK_DISPLAY_DDI_IO_B_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_IO))
#define GLK_DISPLAY_DDI_IO_C_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_IO))
#define GLK_DPIO_CMN_A_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_A_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define GLK_DPIO_CMN_B_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define GLK_DPIO_CMN_C_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define GLK_DISPLAY_AUX_A_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_A) |		\
	BIT_ULL(POWER_DOMAIN_AUX_IO_A) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define GLK_DISPLAY_AUX_B_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define GLK_DISPLAY_AUX_C_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_C) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define GLK_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	GLK_DISPLAY_POWERWELL_2_POWER_DOMAINS |		\
	BIT_ULL(POWER_DOMAIN_GT_IRQ) |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_GMBUS) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define CNL_DISPLAY_POWERWELL_2_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_LANES) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |                       \
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_AUX_D) |			\
	BIT_ULL(POWER_DOMAIN_AUX_F) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_VGA) |				\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_DDI_A_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_A_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_DDI_B_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_DDI_C_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_DDI_D_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_AUX_A_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_AUX_IO_A) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_AUX_B_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_AUX_C_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_AUX_D_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_D) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_AUX_F_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_F) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_DDI_F_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_IO) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
#define CNL_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	CNL_DISPLAY_POWERWELL_2_POWER_DOMAINS |		\
	BIT_ULL(POWER_DOMAIN_GT_IRQ) |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

/*
 * ICL PW_0/PG_0 domains (HW/DMC control):
 * - PCI
 * - clocks except port PLL
 * - central power except FBC
 * - shared functions except pipe interrupts, pipe MBUS, DBUF registers
 * ICL PW_1/PG_1 domains (HW/DMC control):
 * - DBUF function
 * - PIPE_A and its planes, except VGA
 * - transcoder EDP + PSR
 * - transcoder DSI
 * - DDI_A
 * - FBC
 */
#define ICL_PW_4_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_INIT))
	/* VDSC/joining */
#define ICL_PW_3_POWER_DOMAINS (			\
	ICL_PW_4_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_A) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_AUX_D) |			\
	BIT_ULL(POWER_DOMAIN_AUX_E) |			\
	BIT_ULL(POWER_DOMAIN_AUX_F) |			\
	BIT_ULL(POWER_DOMAIN_AUX_C_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_D_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_E_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_F_TBT) |		\
	BIT_ULL(POWER_DOMAIN_VGA) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
	/*
	 * - transcoder WD
	 * - KVMR (HW control)
	 */
#define ICL_PW_2_POWER_DOMAINS (			\
	ICL_PW_3_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_VDSC_PW2) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
	/*
	 * - KVMR (HW control)
	 */
#define ICL_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	ICL_PW_2_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_DPLL_DC_OFF) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define ICL_DDI_IO_A_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_A_IO))
#define ICL_DDI_IO_B_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_IO))
#define ICL_DDI_IO_C_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_IO))
#define ICL_DDI_IO_D_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_IO))
#define ICL_DDI_IO_E_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_IO))
#define ICL_DDI_IO_F_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_IO))

#define ICL_AUX_A_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_IO_A) |		\
	BIT_ULL(POWER_DOMAIN_AUX_A))
#define ICL_AUX_B_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_B))
#define ICL_AUX_C_TC1_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_C))
#define ICL_AUX_D_TC2_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_D))
#define ICL_AUX_E_TC3_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_E))
#define ICL_AUX_F_TC4_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_F))
#define ICL_AUX_C_TBT1_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_C_TBT))
#define ICL_AUX_D_TBT2_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_D_TBT))
#define ICL_AUX_E_TBT3_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_E_TBT))
#define ICL_AUX_F_TBT4_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_F_TBT))

#define TGL_PW_5_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PIPE_D) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_D) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_D_PANEL_FITTER) |     \
	BIT_ULL(POWER_DOMAIN_INIT))

#define TGL_PW_4_POWER_DOMAINS (			\
	TGL_PW_5_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_INIT))

#define TGL_PW_3_POWER_DOMAINS (			\
	TGL_PW_4_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_G_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_H_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_I_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_D) |			\
	BIT_ULL(POWER_DOMAIN_AUX_E) |			\
	BIT_ULL(POWER_DOMAIN_AUX_F) |			\
	BIT_ULL(POWER_DOMAIN_AUX_G) |			\
	BIT_ULL(POWER_DOMAIN_AUX_H) |			\
	BIT_ULL(POWER_DOMAIN_AUX_I) |			\
	BIT_ULL(POWER_DOMAIN_AUX_D_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_E_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_F_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_G_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_H_TBT) |		\
	BIT_ULL(POWER_DOMAIN_AUX_I_TBT) |		\
	BIT_ULL(POWER_DOMAIN_VGA) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define TGL_PW_2_POWER_DOMAINS (			\
	TGL_PW_3_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_VDSC_PW2) |	\
	BIT_ULL(POWER_DOMAIN_INIT))

#define TGL_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	TGL_PW_3_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

#define TGL_DDI_IO_D_TC1_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_IO))
#define TGL_DDI_IO_E_TC2_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_IO))
#define TGL_DDI_IO_F_TC3_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_IO))
#define TGL_DDI_IO_G_TC4_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_G_IO))
#define TGL_DDI_IO_H_TC5_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_H_IO))
#define TGL_DDI_IO_I_TC6_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_I_IO))

#define TGL_AUX_A_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_IO_A) |	\
	BIT_ULL(POWER_DOMAIN_AUX_A))
#define TGL_AUX_B_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_B))
#define TGL_AUX_C_IO_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_C))
#define TGL_AUX_D_TC1_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_D))
#define TGL_AUX_E_TC2_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_E))
#define TGL_AUX_F_TC3_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_F))
#define TGL_AUX_G_TC4_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_G))
#define TGL_AUX_H_TC5_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_H))
#define TGL_AUX_I_TC6_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_I))
#define TGL_AUX_D_TBT1_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_D_TBT))
#define TGL_AUX_E_TBT2_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_E_TBT))
#define TGL_AUX_F_TBT3_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_F_TBT))
#define TGL_AUX_G_TBT4_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_G_TBT))
#define TGL_AUX_H_TBT5_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_H_TBT))
#define TGL_AUX_I_TBT6_IO_POWER_DOMAINS (	\
	BIT_ULL(POWER_DOMAIN_AUX_I_TBT))

#define TGL_TC_COLD_OFF_POWER_DOMAINS (		\
	BIT_ULL(POWER_DOMAIN_AUX_D)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_E)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_F)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_G)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_H)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_I)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_D_TBT)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_E_TBT)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_F_TBT)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_G_TBT)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_H_TBT)	|	\
	BIT_ULL(POWER_DOMAIN_AUX_I_TBT)	|	\
	BIT_ULL(POWER_DOMAIN_TC_COLD_OFF))

#define RKL_PW_4_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_PIPE_C) |			\
	BIT_ULL(POWER_DOMAIN_PIPE_C_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_C) |		\
	BIT_ULL(POWER_DOMAIN_INIT))

#define RKL_PW_3_POWER_DOMAINS (			\
	RKL_PW_4_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_PIPE_B) |			\
	BIT_ULL(POWER_DOMAIN_PIPE_B_PANEL_FITTER) |	\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_VGA) |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_B) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_LANES) |	\
	BIT_ULL(POWER_DOMAIN_AUX_D) |			\
	BIT_ULL(POWER_DOMAIN_AUX_E) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

/*
 * There is no PW_2/PG_2 on RKL.
 *
 * RKL PW_1/PG_1 domains (under HW/DMC control):
 * - DBUF function (note: registers are in PW0)
 * - PIPE_A and its planes and VDSC/joining, except VGA
 * - transcoder A
 * - DDI_A and DDI_B
 * - FBC
 *
 * RKL PW_0/PG_0 domains (under HW/DMC control):
 * - PCI
 * - clocks except port PLL
 * - shared functions:
 *     * interrupts except pipe interrupts
 *     * MBus except PIPE_MBUS_DBOX_CTL
 *     * DBUF registers
 * - central power except FBC
 * - top-level GTC (DDI-level GTC is in the well associated with the DDI)
 */

#define RKL_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	RKL_PW_3_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_INIT))

static const struct i915_power_well_ops i9xx_always_on_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
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
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = chv_dpio_cmn_power_well_enable,
	.disable = chv_dpio_cmn_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_desc i9xx_always_on_power_well[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
};

static const struct i915_power_well_ops i830_pipes_power_well_ops = {
	.sync_hw = i830_pipes_power_well_sync_hw,
	.enable = i830_pipes_power_well_enable,
	.disable = i830_pipes_power_well_disable,
	.is_enabled = i830_pipes_power_well_enabled,
};

static const struct i915_power_well_desc i830_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "pipes",
		.domains = I830_PIPES_POWER_DOMAINS,
		.ops = &i830_pipes_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
};

static const struct i915_power_well_ops hsw_power_well_ops = {
	.sync_hw = hsw_power_well_sync_hw,
	.enable = hsw_power_well_enable,
	.disable = hsw_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

static const struct i915_power_well_ops gen9_dc_off_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = gen9_dc_off_power_well_enable,
	.disable = gen9_dc_off_power_well_disable,
	.is_enabled = gen9_dc_off_power_well_enabled,
};

static const struct i915_power_well_ops bxt_dpio_cmn_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = bxt_dpio_cmn_power_well_enable,
	.disable = bxt_dpio_cmn_power_well_disable,
	.is_enabled = bxt_dpio_cmn_power_well_enabled,
};

static const struct i915_power_well_regs hsw_power_well_regs = {
	.bios	= HSW_PWR_WELL_CTL1,
	.driver	= HSW_PWR_WELL_CTL2,
	.kvmr	= HSW_PWR_WELL_CTL3,
	.debug	= HSW_PWR_WELL_CTL4,
};

static const struct i915_power_well_desc hsw_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "display",
		.domains = HSW_DISPLAY_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = HSW_DISP_PW_GLOBAL,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = HSW_PW_CTL_IDX_GLOBAL,
			.hsw.has_vga = true,
		},
	},
};

static const struct i915_power_well_desc bdw_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "display",
		.domains = BDW_DISPLAY_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = HSW_DISP_PW_GLOBAL,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = HSW_PW_CTL_IDX_GLOBAL,
			.hsw.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
			.hsw.has_vga = true,
		},
	},
};

static const struct i915_power_well_ops vlv_display_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = vlv_display_power_well_enable,
	.disable = vlv_display_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_ops vlv_dpio_cmn_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = vlv_dpio_cmn_power_well_enable,
	.disable = vlv_dpio_cmn_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_ops vlv_dpio_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = vlv_power_well_enable,
	.disable = vlv_power_well_disable,
	.is_enabled = vlv_power_well_enabled,
};

static const struct i915_power_well_desc vlv_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "display",
		.domains = VLV_DISPLAY_POWER_DOMAINS,
		.ops = &vlv_display_power_well_ops,
		.id = VLV_DISP_PW_DISP2D,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DISP2D,
		},
	},
	{
		.name = "dpio-tx-b-01",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_B_LANES_01,
		},
	},
	{
		.name = "dpio-tx-b-23",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_B_LANES_23,
		},
	},
	{
		.name = "dpio-tx-c-01",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_C_LANES_01,
		},
	},
	{
		.name = "dpio-tx-c-23",
		.domains = VLV_DPIO_TX_B_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_B_LANES_23_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_01_POWER_DOMAINS |
			   VLV_DPIO_TX_C_LANES_23_POWER_DOMAINS,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_C_LANES_23,
		},
	},
	{
		.name = "dpio-common",
		.domains = VLV_DPIO_CMN_BC_POWER_DOMAINS,
		.ops = &vlv_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_BC,
		},
	},
};

static const struct i915_power_well_desc chv_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "display",
		/*
		 * Pipe A power well is the new disp2d well. Pipe B and C
		 * power wells don't actually exist. Pipe A power well is
		 * required for any pipe to work.
		 */
		.domains = CHV_DISPLAY_POWER_DOMAINS,
		.ops = &chv_pipe_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "dpio-common-bc",
		.domains = CHV_DPIO_CMN_BC_POWER_DOMAINS,
		.ops = &chv_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_BC,
		},
	},
	{
		.name = "dpio-common-d",
		.domains = CHV_DPIO_CMN_D_POWER_DOMAINS,
		.ops = &chv_dpio_cmn_power_well_ops,
		.id = CHV_DISP_PW_DPIO_CMN_D,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_D,
		},
	},
};

bool intel_display_power_well_is_enabled(struct drm_i915_private *dev_priv,
					 enum i915_power_well_id power_well_id)
{
	struct i915_power_well *power_well;
	bool ret;

	power_well = lookup_power_well(dev_priv, power_well_id);
	ret = power_well->desc->ops->is_enabled(dev_priv, power_well);

	return ret;
}

static const struct i915_power_well_desc skl_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "power well 1",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_1,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_1,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "MISC IO power well",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_MISC_IO,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_MISC_IO,
		},
	},
	{
		.name = "DC off",
		.domains = SKL_DISPLAY_DC_OFF_POWER_DOMAINS,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	},
	{
		.name = "power well 2",
		.domains = SKL_DISPLAY_POWERWELL_2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_2,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_2,
			.hsw.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
			.hsw.has_vga = true,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DDI A/E IO power well",
		.domains = SKL_DISPLAY_DDI_IO_A_E_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_A_E,
		},
	},
	{
		.name = "DDI B IO power well",
		.domains = SKL_DISPLAY_DDI_IO_B_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_B,
		},
	},
	{
		.name = "DDI C IO power well",
		.domains = SKL_DISPLAY_DDI_IO_C_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_C,
		},
	},
	{
		.name = "DDI D IO power well",
		.domains = SKL_DISPLAY_DDI_IO_D_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_D,
		},
	},
};

static const struct i915_power_well_desc bxt_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "power well 1",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_1,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_1,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DC off",
		.domains = BXT_DISPLAY_DC_OFF_POWER_DOMAINS,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	},
	{
		.name = "power well 2",
		.domains = BXT_DISPLAY_POWERWELL_2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_2,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_2,
			.hsw.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
			.hsw.has_vga = true,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "dpio-common-a",
		.domains = BXT_DPIO_CMN_A_POWER_DOMAINS,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = BXT_DISP_PW_DPIO_CMN_A,
		{
			.bxt.phy = DPIO_PHY1,
		},
	},
	{
		.name = "dpio-common-bc",
		.domains = BXT_DPIO_CMN_BC_POWER_DOMAINS,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.bxt.phy = DPIO_PHY0,
		},
	},
};

static const struct i915_power_well_desc glk_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "power well 1",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_1,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_1,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DC off",
		.domains = GLK_DISPLAY_DC_OFF_POWER_DOMAINS,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	},
	{
		.name = "power well 2",
		.domains = GLK_DISPLAY_POWERWELL_2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_2,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_2,
			.hsw.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
			.hsw.has_vga = true,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "dpio-common-a",
		.domains = GLK_DPIO_CMN_A_POWER_DOMAINS,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = BXT_DISP_PW_DPIO_CMN_A,
		{
			.bxt.phy = DPIO_PHY1,
		},
	},
	{
		.name = "dpio-common-b",
		.domains = GLK_DPIO_CMN_B_POWER_DOMAINS,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.bxt.phy = DPIO_PHY0,
		},
	},
	{
		.name = "dpio-common-c",
		.domains = GLK_DPIO_CMN_C_POWER_DOMAINS,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = GLK_DISP_PW_DPIO_CMN_C,
		{
			.bxt.phy = DPIO_PHY2,
		},
	},
	{
		.name = "AUX A",
		.domains = GLK_DISPLAY_AUX_A_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_AUX_A,
		},
	},
	{
		.name = "AUX B",
		.domains = GLK_DISPLAY_AUX_B_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_AUX_B,
		},
	},
	{
		.name = "AUX C",
		.domains = GLK_DISPLAY_AUX_C_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_AUX_C,
		},
	},
	{
		.name = "DDI A IO power well",
		.domains = GLK_DISPLAY_DDI_IO_A_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_DDI_A,
		},
	},
	{
		.name = "DDI B IO power well",
		.domains = GLK_DISPLAY_DDI_IO_B_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_B,
		},
	},
	{
		.name = "DDI C IO power well",
		.domains = GLK_DISPLAY_DDI_IO_C_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_C,
		},
	},
};

static const struct i915_power_well_desc cnl_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "power well 1",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_1,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_1,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "AUX A",
		.domains = CNL_DISPLAY_AUX_A_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_AUX_A,
		},
	},
	{
		.name = "AUX B",
		.domains = CNL_DISPLAY_AUX_B_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_AUX_B,
		},
	},
	{
		.name = "AUX C",
		.domains = CNL_DISPLAY_AUX_C_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_AUX_C,
		},
	},
	{
		.name = "AUX D",
		.domains = CNL_DISPLAY_AUX_D_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = CNL_PW_CTL_IDX_AUX_D,
		},
	},
	{
		.name = "DC off",
		.domains = CNL_DISPLAY_DC_OFF_POWER_DOMAINS,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	},
	{
		.name = "power well 2",
		.domains = CNL_DISPLAY_POWERWELL_2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_2,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_PW_2,
			.hsw.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
			.hsw.has_vga = true,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DDI A IO power well",
		.domains = CNL_DISPLAY_DDI_A_IO_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = GLK_PW_CTL_IDX_DDI_A,
		},
	},
	{
		.name = "DDI B IO power well",
		.domains = CNL_DISPLAY_DDI_B_IO_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_B,
		},
	},
	{
		.name = "DDI C IO power well",
		.domains = CNL_DISPLAY_DDI_C_IO_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_C,
		},
	},
	{
		.name = "DDI D IO power well",
		.domains = CNL_DISPLAY_DDI_D_IO_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = SKL_PW_CTL_IDX_DDI_D,
		},
	},
	{
		.name = "DDI F IO power well",
		.domains = CNL_DISPLAY_DDI_F_IO_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = CNL_DISP_PW_DDI_F_IO,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = CNL_PW_CTL_IDX_DDI_F,
		},
	},
	{
		.name = "AUX F",
		.domains = CNL_DISPLAY_AUX_F_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = CNL_DISP_PW_DDI_F_AUX,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = CNL_PW_CTL_IDX_AUX_F,
		},
	},
};

static const struct i915_power_well_ops icl_aux_power_well_ops = {
	.sync_hw = hsw_power_well_sync_hw,
	.enable = icl_aux_power_well_enable,
	.disable = icl_aux_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

static const struct i915_power_well_regs icl_aux_power_well_regs = {
	.bios	= ICL_PWR_WELL_CTL_AUX1,
	.driver	= ICL_PWR_WELL_CTL_AUX2,
	.debug	= ICL_PWR_WELL_CTL_AUX4,
};

static const struct i915_power_well_regs icl_ddi_power_well_regs = {
	.bios	= ICL_PWR_WELL_CTL_DDI1,
	.driver	= ICL_PWR_WELL_CTL_DDI2,
	.debug	= ICL_PWR_WELL_CTL_DDI4,
};

static const struct i915_power_well_desc icl_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "power well 1",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_1,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DC off",
		.domains = ICL_DISPLAY_DC_OFF_POWER_DOMAINS,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	},
	{
		.name = "power well 2",
		.domains = ICL_PW_2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_2,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_2,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "power well 3",
		.domains = ICL_PW_3_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = ICL_DISP_PW_3,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_3,
			.hsw.irq_pipe_mask = BIT(PIPE_B),
			.hsw.has_vga = true,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DDI A IO",
		.domains = ICL_DDI_IO_A_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		},
	},
	{
		.name = "DDI B IO",
		.domains = ICL_DDI_IO_B_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		},
	},
	{
		.name = "DDI C IO",
		.domains = ICL_DDI_IO_C_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_C,
		},
	},
	{
		.name = "DDI D IO",
		.domains = ICL_DDI_IO_D_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_D,
		},
	},
	{
		.name = "DDI E IO",
		.domains = ICL_DDI_IO_E_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_E,
		},
	},
	{
		.name = "DDI F IO",
		.domains = ICL_DDI_IO_F_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_F,
		},
	},
	{
		.name = "AUX A",
		.domains = ICL_AUX_A_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	},
	{
		.name = "AUX B",
		.domains = ICL_AUX_B_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	},
	{
		.name = "AUX C TC1",
		.domains = ICL_AUX_C_TC1_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_C,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX D TC2",
		.domains = ICL_AUX_D_TC2_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_D,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX E TC3",
		.domains = ICL_AUX_E_TC3_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_E,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX F TC4",
		.domains = ICL_AUX_F_TC4_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_F,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX C TBT1",
		.domains = ICL_AUX_C_TBT1_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT1,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX D TBT2",
		.domains = ICL_AUX_D_TBT2_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT2,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX E TBT3",
		.domains = ICL_AUX_E_TBT3_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT3,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX F TBT4",
		.domains = ICL_AUX_F_TBT4_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT4,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "power well 4",
		.domains = ICL_PW_4_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_4,
			.hsw.has_fuses = true,
			.hsw.irq_pipe_mask = BIT(PIPE_C),
		},
	},
};

static void
tgl_tc_cold_request(struct drm_i915_private *i915, bool block)
{
	u8 tries = 0;
	int ret;

	while (1) {
		u32 low_val;
		u32 high_val = 0;

		if (block)
			low_val = TGL_PCODE_EXIT_TCCOLD_DATA_L_BLOCK_REQ;
		else
			low_val = TGL_PCODE_EXIT_TCCOLD_DATA_L_UNBLOCK_REQ;

		/*
		 * Spec states that we should timeout the request after 200us
		 * but the function below will timeout after 500us
		 */
		ret = sandybridge_pcode_read(i915, TGL_PCODE_TCCOLD, &low_val,
					     &high_val);
		if (ret == 0) {
			if (block &&
			    (low_val & TGL_PCODE_EXIT_TCCOLD_DATA_L_EXIT_FAILED))
				ret = -EIO;
			else
				break;
		}

		if (++tries == 3)
			break;

		msleep(1);
	}

	if (ret)
		drm_err(&i915->drm, "TC cold %sblock failed\n",
			block ? "" : "un");
	else
		drm_dbg_kms(&i915->drm, "TC cold %sblock succeeded\n",
			    block ? "" : "un");
}

static void
tgl_tc_cold_off_power_well_enable(struct drm_i915_private *i915,
				  struct i915_power_well *power_well)
{
	tgl_tc_cold_request(i915, true);
}

static void
tgl_tc_cold_off_power_well_disable(struct drm_i915_private *i915,
				   struct i915_power_well *power_well)
{
	tgl_tc_cold_request(i915, false);
}

static void
tgl_tc_cold_off_power_well_sync_hw(struct drm_i915_private *i915,
				   struct i915_power_well *power_well)
{
	if (power_well->count > 0)
		tgl_tc_cold_off_power_well_enable(i915, power_well);
	else
		tgl_tc_cold_off_power_well_disable(i915, power_well);
}

static bool
tgl_tc_cold_off_power_well_is_enabled(struct drm_i915_private *dev_priv,
				      struct i915_power_well *power_well)
{
	/*
	 * Not the correctly implementation but there is no way to just read it
	 * from PCODE, so returning count to avoid state mismatch errors
	 */
	return power_well->count;
}

static const struct i915_power_well_ops tgl_tc_cold_off_ops = {
	.sync_hw = tgl_tc_cold_off_power_well_sync_hw,
	.enable = tgl_tc_cold_off_power_well_enable,
	.disable = tgl_tc_cold_off_power_well_disable,
	.is_enabled = tgl_tc_cold_off_power_well_is_enabled,
};

static const struct i915_power_well_desc tgl_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "power well 1",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_1,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DC off",
		.domains = TGL_DISPLAY_DC_OFF_POWER_DOMAINS,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	},
	{
		.name = "power well 2",
		.domains = TGL_PW_2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_2,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_2,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "power well 3",
		.domains = TGL_PW_3_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = ICL_DISP_PW_3,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_3,
			.hsw.irq_pipe_mask = BIT(PIPE_B),
			.hsw.has_vga = true,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DDI A IO",
		.domains = ICL_DDI_IO_A_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		}
	},
	{
		.name = "DDI B IO",
		.domains = ICL_DDI_IO_B_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		}
	},
	{
		.name = "DDI C IO",
		.domains = ICL_DDI_IO_C_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_C,
		}
	},
	{
		.name = "DDI D TC1 IO",
		.domains = TGL_DDI_IO_D_TC1_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC1,
		},
	},
	{
		.name = "DDI E TC2 IO",
		.domains = TGL_DDI_IO_E_TC2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC2,
		},
	},
	{
		.name = "DDI F TC3 IO",
		.domains = TGL_DDI_IO_F_TC3_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC3,
		},
	},
	{
		.name = "DDI G TC4 IO",
		.domains = TGL_DDI_IO_G_TC4_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC4,
		},
	},
	{
		.name = "DDI H TC5 IO",
		.domains = TGL_DDI_IO_H_TC5_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC5,
		},
	},
	{
		.name = "DDI I TC6 IO",
		.domains = TGL_DDI_IO_I_TC6_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC6,
		},
	},
	{
		.name = "TC cold off",
		.domains = TGL_TC_COLD_OFF_POWER_DOMAINS,
		.ops = &tgl_tc_cold_off_ops,
		.id = TGL_DISP_PW_TC_COLD_OFF,
	},
	{
		.name = "AUX A",
		.domains = TGL_AUX_A_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	},
	{
		.name = "AUX B",
		.domains = TGL_AUX_B_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	},
	{
		.name = "AUX C",
		.domains = TGL_AUX_C_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_C,
		},
	},
	{
		.name = "AUX D TC1",
		.domains = TGL_AUX_D_TC1_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC1,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX E TC2",
		.domains = TGL_AUX_E_TC2_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC2,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX F TC3",
		.domains = TGL_AUX_F_TC3_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC3,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX G TC4",
		.domains = TGL_AUX_G_TC4_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC4,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX H TC5",
		.domains = TGL_AUX_H_TC5_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC5,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX I TC6",
		.domains = TGL_AUX_I_TC6_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC6,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX D TBT1",
		.domains = TGL_AUX_D_TBT1_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT1,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX E TBT2",
		.domains = TGL_AUX_E_TBT2_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT2,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX F TBT3",
		.domains = TGL_AUX_F_TBT3_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT3,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX G TBT4",
		.domains = TGL_AUX_G_TBT4_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT4,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX H TBT5",
		.domains = TGL_AUX_H_TBT5_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT5,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX I TBT6",
		.domains = TGL_AUX_I_TBT6_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT6,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "power well 4",
		.domains = TGL_PW_4_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_4,
			.hsw.has_fuses = true,
			.hsw.irq_pipe_mask = BIT(PIPE_C),
		}
	},
	{
		.name = "power well 5",
		.domains = TGL_PW_5_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_PW_5,
			.hsw.has_fuses = true,
			.hsw.irq_pipe_mask = BIT(PIPE_D),
		},
	},
};

static const struct i915_power_well_desc rkl_power_wells[] = {
	{
		.name = "always-on",
		.always_on = true,
		.domains = POWER_DOMAIN_MASK,
		.ops = &i9xx_always_on_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
	{
		.name = "power well 1",
		/* Handled by the DMC firmware */
		.always_on = true,
		.domains = 0,
		.ops = &hsw_power_well_ops,
		.id = SKL_DISP_PW_1,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "DC off",
		.domains = RKL_DISPLAY_DC_OFF_POWER_DOMAINS,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	},
	{
		.name = "power well 3",
		.domains = RKL_PW_3_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = ICL_DISP_PW_3,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_3,
			.hsw.irq_pipe_mask = BIT(PIPE_B),
			.hsw.has_vga = true,
			.hsw.has_fuses = true,
		},
	},
	{
		.name = "power well 4",
		.domains = RKL_PW_4_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_PW_4,
			.hsw.has_fuses = true,
			.hsw.irq_pipe_mask = BIT(PIPE_C),
		}
	},
	{
		.name = "DDI A IO",
		.domains = ICL_DDI_IO_A_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		}
	},
	{
		.name = "DDI B IO",
		.domains = ICL_DDI_IO_B_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		}
	},
	{
		.name = "DDI D TC1 IO",
		.domains = TGL_DDI_IO_D_TC1_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC1,
		},
	},
	{
		.name = "DDI E TC2 IO",
		.domains = TGL_DDI_IO_E_TC2_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_ddi_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC2,
		},
	},
	{
		.name = "AUX A",
		.domains = ICL_AUX_A_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	},
	{
		.name = "AUX B",
		.domains = ICL_AUX_B_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	},
	{
		.name = "AUX D TC1",
		.domains = TGL_AUX_D_TC1_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC1,
		},
	},
	{
		.name = "AUX E TC2",
		.domains = TGL_AUX_E_TC2_IO_POWER_DOMAINS,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC2,
		},
	},
};

static int
sanitize_disable_power_well_option(const struct drm_i915_private *dev_priv,
				   int disable_power_well)
{
	if (disable_power_well >= 0)
		return !!disable_power_well;

	return 1;
}

static u32 get_allowed_dc_mask(const struct drm_i915_private *dev_priv,
			       int enable_dc)
{
	u32 mask;
	int requested_dc;
	int max_dc;

	if (INTEL_GEN(dev_priv) >= 12) {
		if (IS_DG1(dev_priv))
			max_dc = 3;
		else
			max_dc = 4;
		/*
		 * DC9 has a separate HW flow from the rest of the DC states,
		 * not depending on the DMC firmware. It's needed by system
		 * suspend/resume, so allow it unconditionally.
		 */
		mask = DC_STATE_EN_DC9;
	} else if (IS_GEN(dev_priv, 11)) {
		max_dc = 2;
		mask = DC_STATE_EN_DC9;
	} else if (IS_GEN(dev_priv, 10) || IS_GEN9_BC(dev_priv)) {
		max_dc = 2;
		mask = 0;
	} else if (IS_GEN9_LP(dev_priv)) {
		max_dc = 1;
		mask = DC_STATE_EN_DC9;
	} else {
		max_dc = 0;
		mask = 0;
	}

	if (!dev_priv->params.disable_power_well)
		max_dc = 0;

	if (enable_dc >= 0 && enable_dc <= max_dc) {
		requested_dc = enable_dc;
	} else if (enable_dc == -1) {
		requested_dc = max_dc;
	} else if (enable_dc > max_dc && enable_dc <= 4) {
		drm_dbg_kms(&dev_priv->drm,
			    "Adjusting requested max DC state (%d->%d)\n",
			    enable_dc, max_dc);
		requested_dc = max_dc;
	} else {
		drm_err(&dev_priv->drm,
			"Unexpected value for enable_dc (%d)\n", enable_dc);
		requested_dc = max_dc;
	}

	switch (requested_dc) {
	case 4:
		mask |= DC_STATE_EN_DC3CO | DC_STATE_EN_UPTO_DC6;
		break;
	case 3:
		mask |= DC_STATE_EN_DC3CO | DC_STATE_EN_UPTO_DC5;
		break;
	case 2:
		mask |= DC_STATE_EN_UPTO_DC6;
		break;
	case 1:
		mask |= DC_STATE_EN_UPTO_DC5;
		break;
	}

	drm_dbg_kms(&dev_priv->drm, "Allowed DC state mask %02x\n", mask);

	return mask;
}

static int
__set_power_wells(struct i915_power_domains *power_domains,
		  const struct i915_power_well_desc *power_well_descs,
		  int power_well_descs_sz, u64 skip_mask)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);
	u64 power_well_ids = 0;
	int power_well_count = 0;
	int i, plt_idx = 0;

	for (i = 0; i < power_well_descs_sz; i++)
		if (!(BIT_ULL(power_well_descs[i].id) & skip_mask))
			power_well_count++;

	power_domains->power_well_count = power_well_count;
	power_domains->power_wells =
				kcalloc(power_well_count,
					sizeof(*power_domains->power_wells),
					GFP_KERNEL);
	if (!power_domains->power_wells)
		return -ENOMEM;

	for (i = 0; i < power_well_descs_sz; i++) {
		enum i915_power_well_id id = power_well_descs[i].id;

		if (BIT_ULL(id) & skip_mask)
			continue;

		power_domains->power_wells[plt_idx++].desc =
			&power_well_descs[i];

		if (id == DISP_PW_ID_NONE)
			continue;

		drm_WARN_ON(&i915->drm, id >= sizeof(power_well_ids) * 8);
		drm_WARN_ON(&i915->drm, power_well_ids & BIT_ULL(id));
		power_well_ids |= BIT_ULL(id);
	}

	return 0;
}

#define set_power_wells_mask(power_domains, __power_well_descs, skip_mask) \
	__set_power_wells(power_domains, __power_well_descs, \
			  ARRAY_SIZE(__power_well_descs), skip_mask)

#define set_power_wells(power_domains, __power_well_descs) \
	set_power_wells_mask(power_domains, __power_well_descs, 0)

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
	int err;

	dev_priv->params.disable_power_well =
		sanitize_disable_power_well_option(dev_priv,
						   dev_priv->params.disable_power_well);
	dev_priv->csr.allowed_dc_mask =
		get_allowed_dc_mask(dev_priv, dev_priv->params.enable_dc);

	dev_priv->csr.target_dc_state =
		sanitize_target_dc_state(dev_priv, DC_STATE_EN_UPTO_DC6);

	BUILD_BUG_ON(POWER_DOMAIN_NUM > 64);

	mutex_init(&power_domains->lock);

	INIT_DELAYED_WORK(&power_domains->async_put_work,
			  intel_display_power_put_async_work);

	/*
	 * The enabling order will be from lower to higher indexed wells,
	 * the disabling order is reversed.
	 */
	if (IS_DG1(dev_priv)) {
		err = set_power_wells_mask(power_domains, tgl_power_wells,
					   BIT_ULL(TGL_DISP_PW_TC_COLD_OFF));
	} else if (IS_ROCKETLAKE(dev_priv)) {
		err = set_power_wells(power_domains, rkl_power_wells);
	} else if (IS_GEN(dev_priv, 12)) {
		err = set_power_wells(power_domains, tgl_power_wells);
	} else if (IS_GEN(dev_priv, 11)) {
		err = set_power_wells(power_domains, icl_power_wells);
	} else if (IS_CNL_WITH_PORT_F(dev_priv)) {
		err = set_power_wells(power_domains, cnl_power_wells);
	} else if (IS_CANNONLAKE(dev_priv)) {
		err = set_power_wells_mask(power_domains, cnl_power_wells,
					   BIT_ULL(CNL_DISP_PW_DDI_F_IO) |
					   BIT_ULL(CNL_DISP_PW_DDI_F_AUX));
	} else if (IS_GEMINILAKE(dev_priv)) {
		err = set_power_wells(power_domains, glk_power_wells);
	} else if (IS_BROXTON(dev_priv)) {
		err = set_power_wells(power_domains, bxt_power_wells);
	} else if (IS_GEN9_BC(dev_priv)) {
		err = set_power_wells(power_domains, skl_power_wells);
	} else if (IS_CHERRYVIEW(dev_priv)) {
		err = set_power_wells(power_domains, chv_power_wells);
	} else if (IS_BROADWELL(dev_priv)) {
		err = set_power_wells(power_domains, bdw_power_wells);
	} else if (IS_HASWELL(dev_priv)) {
		err = set_power_wells(power_domains, hsw_power_wells);
	} else if (IS_VALLEYVIEW(dev_priv)) {
		err = set_power_wells(power_domains, vlv_power_wells);
	} else if (IS_I830(dev_priv)) {
		err = set_power_wells(power_domains, i830_power_wells);
	} else {
		err = set_power_wells(power_domains, i9xx_always_on_power_well);
	}

	return err;
}

/**
 * intel_power_domains_cleanup - clean up power domains resources
 * @dev_priv: i915 device instance
 *
 * Release any resources acquired by intel_power_domains_init()
 */
void intel_power_domains_cleanup(struct drm_i915_private *dev_priv)
{
	kfree(dev_priv->power_domains.power_wells);
}

static void intel_power_domains_sync_hw(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;

	mutex_lock(&power_domains->lock);
	for_each_power_well(dev_priv, power_well) {
		power_well->desc->ops->sync_hw(dev_priv, power_well);
		power_well->hw_enabled =
			power_well->desc->ops->is_enabled(dev_priv, power_well);
	}
	mutex_unlock(&power_domains->lock);
}

static void gen9_dbuf_slice_set(struct drm_i915_private *dev_priv,
				enum dbuf_slice slice, bool enable)
{
	i915_reg_t reg = DBUF_CTL_S(slice);
	bool state;
	u32 val;

	val = intel_de_read(dev_priv, reg);
	if (enable)
		val |= DBUF_POWER_REQUEST;
	else
		val &= ~DBUF_POWER_REQUEST;
	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);
	udelay(10);

	state = intel_de_read(dev_priv, reg) & DBUF_POWER_STATE;
	drm_WARN(&dev_priv->drm, enable != state,
		 "DBuf slice %d power %s timeout!\n",
		 slice, enable ? "enable" : "disable");
}

void gen9_dbuf_slices_update(struct drm_i915_private *dev_priv,
			     u8 req_slices)
{
	int num_slices = INTEL_INFO(dev_priv)->num_supported_dbuf_slices;
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	enum dbuf_slice slice;

	drm_WARN(&dev_priv->drm, req_slices & ~(BIT(num_slices) - 1),
		 "Invalid set of dbuf slices (0x%x) requested (num dbuf slices %d)\n",
		 req_slices, num_slices);

	drm_dbg_kms(&dev_priv->drm, "Updating dbuf slices to 0x%x\n",
		    req_slices);

	/*
	 * Might be running this in parallel to gen9_dc_off_power_well_enable
	 * being called from intel_dp_detect for instance,
	 * which causes assertion triggered by race condition,
	 * as gen9_assert_dbuf_enabled might preempt this when registers
	 * were already updated, while dev_priv was not.
	 */
	mutex_lock(&power_domains->lock);

	for (slice = DBUF_S1; slice < num_slices; slice++)
		gen9_dbuf_slice_set(dev_priv, slice, req_slices & BIT(slice));

	dev_priv->dbuf.enabled_slices = req_slices;

	mutex_unlock(&power_domains->lock);
}

static void gen9_dbuf_enable(struct drm_i915_private *dev_priv)
{
	dev_priv->dbuf.enabled_slices =
		intel_enabled_dbuf_slices_mask(dev_priv);

	/*
	 * Just power up at least 1 slice, we will
	 * figure out later which slices we have and what we need.
	 */
	gen9_dbuf_slices_update(dev_priv, BIT(DBUF_S1) |
				dev_priv->dbuf.enabled_slices);
}

static void gen9_dbuf_disable(struct drm_i915_private *dev_priv)
{
	gen9_dbuf_slices_update(dev_priv, 0);
}

static void gen12_dbuf_slices_config(struct drm_i915_private *dev_priv)
{
	const int num_slices = INTEL_INFO(dev_priv)->num_supported_dbuf_slices;
	enum dbuf_slice slice;

	for (slice = DBUF_S1; slice < (DBUF_S1 + num_slices); slice++)
		intel_de_rmw(dev_priv, DBUF_CTL_S(slice),
			     DBUF_TRACKER_STATE_SERVICE_MASK,
			     DBUF_TRACKER_STATE_SERVICE(8));
}

static void icl_mbus_init(struct drm_i915_private *dev_priv)
{
	unsigned long abox_regs = INTEL_INFO(dev_priv)->abox_mask;
	u32 mask, val, i;

	mask = MBUS_ABOX_BT_CREDIT_POOL1_MASK |
		MBUS_ABOX_BT_CREDIT_POOL2_MASK |
		MBUS_ABOX_B_CREDIT_MASK |
		MBUS_ABOX_BW_CREDIT_MASK;
	val = MBUS_ABOX_BT_CREDIT_POOL1(16) |
		MBUS_ABOX_BT_CREDIT_POOL2(16) |
		MBUS_ABOX_B_CREDIT(1) |
		MBUS_ABOX_BW_CREDIT(1);

	/*
	 * gen12 platforms that use abox1 and abox2 for pixel data reads still
	 * expect us to program the abox_ctl0 register as well, even though
	 * we don't have to program other instance-0 registers like BW_BUDDY.
	 */
	if (IS_GEN(dev_priv, 12))
		abox_regs |= BIT(0);

	for_each_set_bit(i, &abox_regs, sizeof(abox_regs))
		intel_de_rmw(dev_priv, MBUS_ABOX_CTL(i), mask, val);
}

static void hsw_assert_cdclk(struct drm_i915_private *dev_priv)
{
	u32 val = intel_de_read(dev_priv, LCPLL_CTL);

	/*
	 * The LCPLL register should be turned on by the BIOS. For now
	 * let's just check its state and print errors in case
	 * something is wrong.  Don't even try to turn it on.
	 */

	if (val & LCPLL_CD_SOURCE_FCLK)
		drm_err(&dev_priv->drm, "CDCLK source is not LCPLL\n");

	if (val & LCPLL_PLL_DISABLE)
		drm_err(&dev_priv->drm, "LCPLL is disabled\n");

	if ((val & LCPLL_REF_MASK) != LCPLL_REF_NON_SSC)
		drm_err(&dev_priv->drm, "LCPLL not using non-SSC reference\n");
}

static void assert_can_disable_lcpll(struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;

	for_each_intel_crtc(dev, crtc)
		I915_STATE_WARN(crtc->active, "CRTC for pipe %c enabled\n",
				pipe_name(crtc->pipe));

	I915_STATE_WARN(intel_de_read(dev_priv, HSW_PWR_WELL_CTL2),
			"Display power well on\n");
	I915_STATE_WARN(intel_de_read(dev_priv, SPLL_CTL) & SPLL_PLL_ENABLE,
			"SPLL enabled\n");
	I915_STATE_WARN(intel_de_read(dev_priv, WRPLL_CTL(0)) & WRPLL_PLL_ENABLE,
			"WRPLL1 enabled\n");
	I915_STATE_WARN(intel_de_read(dev_priv, WRPLL_CTL(1)) & WRPLL_PLL_ENABLE,
			"WRPLL2 enabled\n");
	I915_STATE_WARN(intel_de_read(dev_priv, PP_STATUS(0)) & PP_ON,
			"Panel power on\n");
	I915_STATE_WARN(intel_de_read(dev_priv, BLC_PWM_CPU_CTL2) & BLM_PWM_ENABLE,
			"CPU PWM1 enabled\n");
	if (IS_HASWELL(dev_priv))
		I915_STATE_WARN(intel_de_read(dev_priv, HSW_BLC_PWM2_CTL) & BLM_PWM_ENABLE,
				"CPU PWM2 enabled\n");
	I915_STATE_WARN(intel_de_read(dev_priv, BLC_PWM_PCH_CTL1) & BLM_PCH_PWM_ENABLE,
			"PCH PWM1 enabled\n");
	I915_STATE_WARN(intel_de_read(dev_priv, UTIL_PIN_CTL) & UTIL_PIN_ENABLE,
			"Utility pin enabled\n");
	I915_STATE_WARN(intel_de_read(dev_priv, PCH_GTC_CTL) & PCH_GTC_ENABLE,
			"PCH GTC enabled\n");

	/*
	 * In theory we can still leave IRQs enabled, as long as only the HPD
	 * interrupts remain enabled. We used to check for that, but since it's
	 * gen-specific and since we only disable LCPLL after we fully disable
	 * the interrupts, the check below should be enough.
	 */
	I915_STATE_WARN(intel_irqs_enabled(dev_priv), "IRQs enabled\n");
}

static u32 hsw_read_dcomp(struct drm_i915_private *dev_priv)
{
	if (IS_HASWELL(dev_priv))
		return intel_de_read(dev_priv, D_COMP_HSW);
	else
		return intel_de_read(dev_priv, D_COMP_BDW);
}

static void hsw_write_dcomp(struct drm_i915_private *dev_priv, u32 val)
{
	if (IS_HASWELL(dev_priv)) {
		if (sandybridge_pcode_write(dev_priv,
					    GEN6_PCODE_WRITE_D_COMP, val))
			drm_dbg_kms(&dev_priv->drm,
				    "Failed to write to D_COMP\n");
	} else {
		intel_de_write(dev_priv, D_COMP_BDW, val);
		intel_de_posting_read(dev_priv, D_COMP_BDW);
	}
}

/*
 * This function implements pieces of two sequences from BSpec:
 * - Sequence for display software to disable LCPLL
 * - Sequence for display software to allow package C8+
 * The steps implemented here are just the steps that actually touch the LCPLL
 * register. Callers should take care of disabling all the display engine
 * functions, doing the mode unset, fixing interrupts, etc.
 */
static void hsw_disable_lcpll(struct drm_i915_private *dev_priv,
			      bool switch_to_fclk, bool allow_power_down)
{
	u32 val;

	assert_can_disable_lcpll(dev_priv);

	val = intel_de_read(dev_priv, LCPLL_CTL);

	if (switch_to_fclk) {
		val |= LCPLL_CD_SOURCE_FCLK;
		intel_de_write(dev_priv, LCPLL_CTL, val);

		if (wait_for_us(intel_de_read(dev_priv, LCPLL_CTL) &
				LCPLL_CD_SOURCE_FCLK_DONE, 1))
			drm_err(&dev_priv->drm, "Switching to FCLK failed\n");

		val = intel_de_read(dev_priv, LCPLL_CTL);
	}

	val |= LCPLL_PLL_DISABLE;
	intel_de_write(dev_priv, LCPLL_CTL, val);
	intel_de_posting_read(dev_priv, LCPLL_CTL);

	if (intel_de_wait_for_clear(dev_priv, LCPLL_CTL, LCPLL_PLL_LOCK, 1))
		drm_err(&dev_priv->drm, "LCPLL still locked\n");

	val = hsw_read_dcomp(dev_priv);
	val |= D_COMP_COMP_DISABLE;
	hsw_write_dcomp(dev_priv, val);
	ndelay(100);

	if (wait_for((hsw_read_dcomp(dev_priv) &
		      D_COMP_RCOMP_IN_PROGRESS) == 0, 1))
		drm_err(&dev_priv->drm, "D_COMP RCOMP still in progress\n");

	if (allow_power_down) {
		val = intel_de_read(dev_priv, LCPLL_CTL);
		val |= LCPLL_POWER_DOWN_ALLOW;
		intel_de_write(dev_priv, LCPLL_CTL, val);
		intel_de_posting_read(dev_priv, LCPLL_CTL);
	}
}

/*
 * Fully restores LCPLL, disallowing power down and switching back to LCPLL
 * source.
 */
static void hsw_restore_lcpll(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = intel_de_read(dev_priv, LCPLL_CTL);

	if ((val & (LCPLL_PLL_LOCK | LCPLL_PLL_DISABLE | LCPLL_CD_SOURCE_FCLK |
		    LCPLL_POWER_DOWN_ALLOW)) == LCPLL_PLL_LOCK)
		return;

	/*
	 * Make sure we're not on PC8 state before disabling PC8, otherwise
	 * we'll hang the machine. To prevent PC8 state, just enable force_wake.
	 */
	intel_uncore_forcewake_get(&dev_priv->uncore, FORCEWAKE_ALL);

	if (val & LCPLL_POWER_DOWN_ALLOW) {
		val &= ~LCPLL_POWER_DOWN_ALLOW;
		intel_de_write(dev_priv, LCPLL_CTL, val);
		intel_de_posting_read(dev_priv, LCPLL_CTL);
	}

	val = hsw_read_dcomp(dev_priv);
	val |= D_COMP_COMP_FORCE;
	val &= ~D_COMP_COMP_DISABLE;
	hsw_write_dcomp(dev_priv, val);

	val = intel_de_read(dev_priv, LCPLL_CTL);
	val &= ~LCPLL_PLL_DISABLE;
	intel_de_write(dev_priv, LCPLL_CTL, val);

	if (intel_de_wait_for_set(dev_priv, LCPLL_CTL, LCPLL_PLL_LOCK, 5))
		drm_err(&dev_priv->drm, "LCPLL not locked yet\n");

	if (val & LCPLL_CD_SOURCE_FCLK) {
		val = intel_de_read(dev_priv, LCPLL_CTL);
		val &= ~LCPLL_CD_SOURCE_FCLK;
		intel_de_write(dev_priv, LCPLL_CTL, val);

		if (wait_for_us((intel_de_read(dev_priv, LCPLL_CTL) &
				 LCPLL_CD_SOURCE_FCLK_DONE) == 0, 1))
			drm_err(&dev_priv->drm,
				"Switching back to LCPLL failed\n");
	}

	intel_uncore_forcewake_put(&dev_priv->uncore, FORCEWAKE_ALL);

	intel_update_cdclk(dev_priv);
	intel_dump_cdclk_config(&dev_priv->cdclk.hw, "Current CDCLK");
}

/*
 * Package states C8 and deeper are really deep PC states that can only be
 * reached when all the devices on the system allow it, so even if the graphics
 * device allows PC8+, it doesn't mean the system will actually get to these
 * states. Our driver only allows PC8+ when going into runtime PM.
 *
 * The requirements for PC8+ are that all the outputs are disabled, the power
 * well is disabled and most interrupts are disabled, and these are also
 * requirements for runtime PM. When these conditions are met, we manually do
 * the other conditions: disable the interrupts, clocks and switch LCPLL refclk
 * to Fclk. If we're in PC8+ and we get an non-hotplug interrupt, we can hard
 * hang the machine.
 *
 * When we really reach PC8 or deeper states (not just when we allow it) we lose
 * the state of some registers, so when we come back from PC8+ we need to
 * restore this state. We don't get into PC8+ if we're not in RC6, so we don't
 * need to take care of the registers kept by RC6. Notice that this happens even
 * if we don't put the device in PCI D3 state (which is what currently happens
 * because of the runtime PM support).
 *
 * For more, read "Display Sequences for Package C8" on the hardware
 * documentation.
 */
static void hsw_enable_pc8(struct drm_i915_private *dev_priv)
{
	u32 val;

	drm_dbg_kms(&dev_priv->drm, "Enabling package C8+\n");

	if (HAS_PCH_LPT_LP(dev_priv)) {
		val = intel_de_read(dev_priv, SOUTH_DSPCLK_GATE_D);
		val &= ~PCH_LP_PARTITION_LEVEL_DISABLE;
		intel_de_write(dev_priv, SOUTH_DSPCLK_GATE_D, val);
	}

	lpt_disable_clkout_dp(dev_priv);
	hsw_disable_lcpll(dev_priv, true, true);
}

static void hsw_disable_pc8(struct drm_i915_private *dev_priv)
{
	u32 val;

	drm_dbg_kms(&dev_priv->drm, "Disabling package C8+\n");

	hsw_restore_lcpll(dev_priv);
	intel_init_pch_refclk(dev_priv);

	if (HAS_PCH_LPT_LP(dev_priv)) {
		val = intel_de_read(dev_priv, SOUTH_DSPCLK_GATE_D);
		val |= PCH_LP_PARTITION_LEVEL_DISABLE;
		intel_de_write(dev_priv, SOUTH_DSPCLK_GATE_D, val);
	}
}

static void intel_pch_reset_handshake(struct drm_i915_private *dev_priv,
				      bool enable)
{
	i915_reg_t reg;
	u32 reset_bits, val;

	if (IS_IVYBRIDGE(dev_priv)) {
		reg = GEN7_MSG_CTL;
		reset_bits = WAIT_FOR_PCH_FLR_ACK | WAIT_FOR_PCH_RESET_ACK;
	} else {
		reg = HSW_NDE_RSTWRN_OPT;
		reset_bits = RESET_PCH_HANDSHAKE_ENABLE;
	}

	val = intel_de_read(dev_priv, reg);

	if (enable)
		val |= reset_bits;
	else
		val &= ~reset_bits;

	intel_de_write(dev_priv, reg, val);
}

static void skl_display_core_init(struct drm_i915_private *dev_priv,
				  bool resume)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	/* enable PCH reset handshake */
	intel_pch_reset_handshake(dev_priv, !HAS_PCH_NOP(dev_priv));

	/* enable PG1 and Misc I/O */
	mutex_lock(&power_domains->lock);

	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_enable(dev_priv, well);

	well = lookup_power_well(dev_priv, SKL_DISP_PW_MISC_IO);
	intel_power_well_enable(dev_priv, well);

	mutex_unlock(&power_domains->lock);

	intel_cdclk_init_hw(dev_priv);

	gen9_dbuf_enable(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);
}

static void skl_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_disable_dc_states(dev_priv);

	gen9_dbuf_disable(dev_priv);

	intel_cdclk_uninit_hw(dev_priv);

	/* The spec doesn't call for removing the reset handshake flag */
	/* disable PG1 and Misc I/O */

	mutex_lock(&power_domains->lock);

	/*
	 * BSpec says to keep the MISC IO power well enabled here, only
	 * remove our request for power well 1.
	 * Note that even though the driver's request is removed power well 1
	 * may stay enabled after this due to DMC's own request on it.
	 */
	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_disable(dev_priv, well);

	mutex_unlock(&power_domains->lock);

	usleep_range(10, 30);		/* 10 us delay per Bspec */
}

static void bxt_display_core_init(struct drm_i915_private *dev_priv, bool resume)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	/*
	 * NDE_RSTWRN_OPT RST PCH Handshake En must always be 0b on BXT
	 * or else the reset will hang because there is no PCH to respond.
	 * Move the handshake programming to initialization sequence.
	 * Previously was left up to BIOS.
	 */
	intel_pch_reset_handshake(dev_priv, false);

	/* Enable PG1 */
	mutex_lock(&power_domains->lock);

	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_enable(dev_priv, well);

	mutex_unlock(&power_domains->lock);

	intel_cdclk_init_hw(dev_priv);

	gen9_dbuf_enable(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);
}

static void bxt_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_disable_dc_states(dev_priv);

	gen9_dbuf_disable(dev_priv);

	intel_cdclk_uninit_hw(dev_priv);

	/* The spec doesn't call for removing the reset handshake flag */

	/*
	 * Disable PW1 (PG1).
	 * Note that even though the driver's request is removed power well 1
	 * may stay enabled after this due to DMC's own request on it.
	 */
	mutex_lock(&power_domains->lock);

	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_disable(dev_priv, well);

	mutex_unlock(&power_domains->lock);

	usleep_range(10, 30);		/* 10 us delay per Bspec */
}

static void cnl_display_core_init(struct drm_i915_private *dev_priv, bool resume)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	/* 1. Enable PCH Reset Handshake */
	intel_pch_reset_handshake(dev_priv, !HAS_PCH_NOP(dev_priv));

	/* 2-3. */
	intel_combo_phy_init(dev_priv);

	/*
	 * 4. Enable Power Well 1 (PG1).
	 *    The AUX IO power wells will be enabled on demand.
	 */
	mutex_lock(&power_domains->lock);
	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_enable(dev_priv, well);
	mutex_unlock(&power_domains->lock);

	/* 5. Enable CD clock */
	intel_cdclk_init_hw(dev_priv);

	/* 6. Enable DBUF */
	gen9_dbuf_enable(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);
}

static void cnl_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_disable_dc_states(dev_priv);

	/* 1. Disable all display engine functions -> aready done */

	/* 2. Disable DBUF */
	gen9_dbuf_disable(dev_priv);

	/* 3. Disable CD clock */
	intel_cdclk_uninit_hw(dev_priv);

	/*
	 * 4. Disable Power Well 1 (PG1).
	 *    The AUX IO power wells are toggled on demand, so they are already
	 *    disabled at this point.
	 */
	mutex_lock(&power_domains->lock);
	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_disable(dev_priv, well);
	mutex_unlock(&power_domains->lock);

	usleep_range(10, 30);		/* 10 us delay per Bspec */

	/* 5. */
	intel_combo_phy_uninit(dev_priv);
}

struct buddy_page_mask {
	u32 page_mask;
	u8 type;
	u8 num_channels;
};

static const struct buddy_page_mask tgl_buddy_page_masks[] = {
	{ .num_channels = 1, .type = INTEL_DRAM_DDR4,   .page_mask = 0xF },
	{ .num_channels = 2, .type = INTEL_DRAM_LPDDR4, .page_mask = 0x1C },
	{ .num_channels = 2, .type = INTEL_DRAM_DDR4,   .page_mask = 0x1F },
	{ .num_channels = 4, .type = INTEL_DRAM_LPDDR4, .page_mask = 0x38 },
	{}
};

static const struct buddy_page_mask wa_1409767108_buddy_page_masks[] = {
	{ .num_channels = 1, .type = INTEL_DRAM_LPDDR4, .page_mask = 0x1 },
	{ .num_channels = 1, .type = INTEL_DRAM_DDR4,   .page_mask = 0x1 },
	{ .num_channels = 2, .type = INTEL_DRAM_LPDDR4, .page_mask = 0x3 },
	{ .num_channels = 2, .type = INTEL_DRAM_DDR4,   .page_mask = 0x3 },
	{}
};

static void tgl_bw_buddy_init(struct drm_i915_private *dev_priv)
{
	enum intel_dram_type type = dev_priv->dram_info.type;
	u8 num_channels = dev_priv->dram_info.num_channels;
	const struct buddy_page_mask *table;
	unsigned long abox_mask = INTEL_INFO(dev_priv)->abox_mask;
	int config, i;

	if (IS_DG1_REVID(dev_priv, DG1_REVID_A0, DG1_REVID_A0) ||
	    IS_TGL_DISP_REVID(dev_priv, TGL_REVID_A0, TGL_REVID_B0))
		/* Wa_1409767108:tgl,dg1 */
		table = wa_1409767108_buddy_page_masks;
	else
		table = tgl_buddy_page_masks;

	for (config = 0; table[config].page_mask != 0; config++)
		if (table[config].num_channels == num_channels &&
		    table[config].type == type)
			break;

	if (table[config].page_mask == 0) {
		drm_dbg(&dev_priv->drm,
			"Unknown memory configuration; disabling address buddy logic.\n");
		for_each_set_bit(i, &abox_mask, sizeof(abox_mask))
			intel_de_write(dev_priv, BW_BUDDY_CTL(i),
				       BW_BUDDY_DISABLE);
	} else {
		for_each_set_bit(i, &abox_mask, sizeof(abox_mask)) {
			intel_de_write(dev_priv, BW_BUDDY_PAGE_MASK(i),
				       table[config].page_mask);

			/* Wa_22010178259:tgl,rkl */
			intel_de_rmw(dev_priv, BW_BUDDY_CTL(i),
				     BW_BUDDY_TLB_REQ_TIMER_MASK,
				     BW_BUDDY_TLB_REQ_TIMER(0x8));
		}
	}
}

static void icl_display_core_init(struct drm_i915_private *dev_priv,
				  bool resume)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;
	u32 val;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	/* Wa_14011294188:ehl,jsl,tgl,rkl */
	if (INTEL_PCH_TYPE(dev_priv) >= PCH_JSP &&
	    INTEL_PCH_TYPE(dev_priv) < PCH_DG1)
		intel_de_rmw(dev_priv, SOUTH_DSPCLK_GATE_D, 0,
			     PCH_DPMGUNIT_CLOCK_GATE_DISABLE);

	/* 1. Enable PCH reset handshake. */
	intel_pch_reset_handshake(dev_priv, !HAS_PCH_NOP(dev_priv));

	/* 2. Initialize all combo phys */
	intel_combo_phy_init(dev_priv);

	/*
	 * 3. Enable Power Well 1 (PG1).
	 *    The AUX IO power wells will be enabled on demand.
	 */
	mutex_lock(&power_domains->lock);
	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_enable(dev_priv, well);
	mutex_unlock(&power_domains->lock);

	/* 4. Enable CDCLK. */
	intel_cdclk_init_hw(dev_priv);

	if (INTEL_GEN(dev_priv) >= 12)
		gen12_dbuf_slices_config(dev_priv);

	/* 5. Enable DBUF. */
	gen9_dbuf_enable(dev_priv);

	/* 6. Setup MBUS. */
	icl_mbus_init(dev_priv);

	/* 7. Program arbiter BW_BUDDY registers */
	if (INTEL_GEN(dev_priv) >= 12)
		tgl_bw_buddy_init(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);

	/* Wa_14011508470 */
	if (IS_GEN(dev_priv, 12)) {
		val = DCPR_CLEAR_MEMSTAT_DIS | DCPR_SEND_RESP_IMM |
		      DCPR_MASK_LPMODE | DCPR_MASK_MAXLATENCY_MEMUP_CLR;
		intel_uncore_rmw(&dev_priv->uncore, GEN11_CHICKEN_DCPR_2, 0, val);
	}
}

static void icl_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_disable_dc_states(dev_priv);

	/* 1. Disable all display engine functions -> aready done */

	/* 2. Disable DBUF */
	gen9_dbuf_disable(dev_priv);

	/* 3. Disable CD clock */
	intel_cdclk_uninit_hw(dev_priv);

	/*
	 * 4. Disable Power Well 1 (PG1).
	 *    The AUX IO power wells are toggled on demand, so they are already
	 *    disabled at this point.
	 */
	mutex_lock(&power_domains->lock);
	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_disable(dev_priv, well);
	mutex_unlock(&power_domains->lock);

	/* 5. */
	intel_combo_phy_uninit(dev_priv);
}

static void chv_phy_control_init(struct drm_i915_private *dev_priv)
{
	struct i915_power_well *cmn_bc =
		lookup_power_well(dev_priv, VLV_DISP_PW_DPIO_CMN_BC);
	struct i915_power_well *cmn_d =
		lookup_power_well(dev_priv, CHV_DISP_PW_DPIO_CMN_D);

	/*
	 * DISPLAY_PHY_CONTROL can get corrupted if read. As a
	 * workaround never ever read DISPLAY_PHY_CONTROL, and
	 * instead maintain a shadow copy ourselves. Use the actual
	 * power well state and lane status to reconstruct the
	 * expected initial value.
	 */
	dev_priv->chv_phy_control =
		PHY_LDO_SEQ_DELAY(PHY_LDO_DELAY_600NS, DPIO_PHY0) |
		PHY_LDO_SEQ_DELAY(PHY_LDO_DELAY_600NS, DPIO_PHY1) |
		PHY_CH_POWER_MODE(PHY_CH_DEEP_PSR, DPIO_PHY0, DPIO_CH0) |
		PHY_CH_POWER_MODE(PHY_CH_DEEP_PSR, DPIO_PHY0, DPIO_CH1) |
		PHY_CH_POWER_MODE(PHY_CH_DEEP_PSR, DPIO_PHY1, DPIO_CH0);

	/*
	 * If all lanes are disabled we leave the override disabled
	 * with all power down bits cleared to match the state we
	 * would use after disabling the port. Otherwise enable the
	 * override and set the lane powerdown bits accding to the
	 * current lane status.
	 */
	if (cmn_bc->desc->ops->is_enabled(dev_priv, cmn_bc)) {
		u32 status = intel_de_read(dev_priv, DPLL(PIPE_A));
		unsigned int mask;

		mask = status & DPLL_PORTB_READY_MASK;
		if (mask == 0xf)
			mask = 0x0;
		else
			dev_priv->chv_phy_control |=
				PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY0, DPIO_CH0);

		dev_priv->chv_phy_control |=
			PHY_CH_POWER_DOWN_OVRD(mask, DPIO_PHY0, DPIO_CH0);

		mask = (status & DPLL_PORTC_READY_MASK) >> 4;
		if (mask == 0xf)
			mask = 0x0;
		else
			dev_priv->chv_phy_control |=
				PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY0, DPIO_CH1);

		dev_priv->chv_phy_control |=
			PHY_CH_POWER_DOWN_OVRD(mask, DPIO_PHY0, DPIO_CH1);

		dev_priv->chv_phy_control |= PHY_COM_LANE_RESET_DEASSERT(DPIO_PHY0);

		dev_priv->chv_phy_assert[DPIO_PHY0] = false;
	} else {
		dev_priv->chv_phy_assert[DPIO_PHY0] = true;
	}

	if (cmn_d->desc->ops->is_enabled(dev_priv, cmn_d)) {
		u32 status = intel_de_read(dev_priv, DPIO_PHY_STATUS);
		unsigned int mask;

		mask = status & DPLL_PORTD_READY_MASK;

		if (mask == 0xf)
			mask = 0x0;
		else
			dev_priv->chv_phy_control |=
				PHY_CH_POWER_DOWN_OVRD_EN(DPIO_PHY1, DPIO_CH0);

		dev_priv->chv_phy_control |=
			PHY_CH_POWER_DOWN_OVRD(mask, DPIO_PHY1, DPIO_CH0);

		dev_priv->chv_phy_control |= PHY_COM_LANE_RESET_DEASSERT(DPIO_PHY1);

		dev_priv->chv_phy_assert[DPIO_PHY1] = false;
	} else {
		dev_priv->chv_phy_assert[DPIO_PHY1] = true;
	}

	drm_dbg_kms(&dev_priv->drm, "Initial PHY_CONTROL=0x%08x\n",
		    dev_priv->chv_phy_control);

	/* Defer application of initial phy_control to enabling the powerwell */
}

static void vlv_cmnlane_wa(struct drm_i915_private *dev_priv)
{
	struct i915_power_well *cmn =
		lookup_power_well(dev_priv, VLV_DISP_PW_DPIO_CMN_BC);
	struct i915_power_well *disp2d =
		lookup_power_well(dev_priv, VLV_DISP_PW_DISP2D);

	/* If the display might be already active skip this */
	if (cmn->desc->ops->is_enabled(dev_priv, cmn) &&
	    disp2d->desc->ops->is_enabled(dev_priv, disp2d) &&
	    intel_de_read(dev_priv, DPIO_CTL) & DPIO_CMNRST)
		return;

	drm_dbg_kms(&dev_priv->drm, "toggling display PHY side reset\n");

	/* cmnlane needs DPLL registers */
	disp2d->desc->ops->enable(dev_priv, disp2d);

	/*
	 * From VLV2A0_DP_eDP_HDMI_DPIO_driver_vbios_notes_11.docx:
	 * Need to assert and de-assert PHY SB reset by gating the
	 * common lane power, then un-gating it.
	 * Simply ungating isn't enough to reset the PHY enough to get
	 * ports and lanes running.
	 */
	cmn->desc->ops->disable(dev_priv, cmn);
}

static bool vlv_punit_is_power_gated(struct drm_i915_private *dev_priv, u32 reg0)
{
	bool ret;

	vlv_punit_get(dev_priv);
	ret = (vlv_punit_read(dev_priv, reg0) & SSPM0_SSC_MASK) == SSPM0_SSC_PWR_GATE;
	vlv_punit_put(dev_priv);

	return ret;
}

static void assert_ved_power_gated(struct drm_i915_private *dev_priv)
{
	drm_WARN(&dev_priv->drm,
		 !vlv_punit_is_power_gated(dev_priv, PUNIT_REG_VEDSSPM0),
		 "VED not power gated\n");
}

static void assert_isp_power_gated(struct drm_i915_private *dev_priv)
{
	static const struct pci_device_id isp_ids[] = {
		{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0f38)},
		{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x22b8)},
		{}
	};

	drm_WARN(&dev_priv->drm, !pci_dev_present(isp_ids) &&
		 !vlv_punit_is_power_gated(dev_priv, PUNIT_REG_ISPSSPM0),
		 "ISP not power gated\n");
}

static void intel_power_domains_verify_state(struct drm_i915_private *dev_priv);

/**
 * intel_power_domains_init_hw - initialize hardware power domain state
 * @i915: i915 device instance
 * @resume: Called from resume code paths or not
 *
 * This function initializes the hardware power domain state and enables all
 * power wells belonging to the INIT power domain. Power wells in other
 * domains (and not in the INIT domain) are referenced or disabled by
 * intel_modeset_readout_hw_state(). After that the reference count of each
 * power well must match its HW enabled state, see
 * intel_power_domains_verify_state().
 *
 * It will return with power domains disabled (to be enabled later by
 * intel_power_domains_enable()) and must be paired with
 * intel_power_domains_driver_remove().
 */
void intel_power_domains_init_hw(struct drm_i915_private *i915, bool resume)
{
	struct i915_power_domains *power_domains = &i915->power_domains;

	power_domains->initializing = true;

	if (INTEL_GEN(i915) >= 11) {
		icl_display_core_init(i915, resume);
	} else if (IS_CANNONLAKE(i915)) {
		cnl_display_core_init(i915, resume);
	} else if (IS_GEN9_BC(i915)) {
		skl_display_core_init(i915, resume);
	} else if (IS_GEN9_LP(i915)) {
		bxt_display_core_init(i915, resume);
	} else if (IS_CHERRYVIEW(i915)) {
		mutex_lock(&power_domains->lock);
		chv_phy_control_init(i915);
		mutex_unlock(&power_domains->lock);
		assert_isp_power_gated(i915);
	} else if (IS_VALLEYVIEW(i915)) {
		mutex_lock(&power_domains->lock);
		vlv_cmnlane_wa(i915);
		mutex_unlock(&power_domains->lock);
		assert_ved_power_gated(i915);
		assert_isp_power_gated(i915);
	} else if (IS_BROADWELL(i915) || IS_HASWELL(i915)) {
		hsw_assert_cdclk(i915);
		intel_pch_reset_handshake(i915, !HAS_PCH_NOP(i915));
	} else if (IS_IVYBRIDGE(i915)) {
		intel_pch_reset_handshake(i915, !HAS_PCH_NOP(i915));
	}

	/*
	 * Keep all power wells enabled for any dependent HW access during
	 * initialization and to make sure we keep BIOS enabled display HW
	 * resources powered until display HW readout is complete. We drop
	 * this reference in intel_power_domains_enable().
	 */
	power_domains->wakeref =
		intel_display_power_get(i915, POWER_DOMAIN_INIT);

	/* Disable power support if the user asked so. */
	if (!i915->params.disable_power_well)
		intel_display_power_get(i915, POWER_DOMAIN_INIT);
	intel_power_domains_sync_hw(i915);

	power_domains->initializing = false;
}

/**
 * intel_power_domains_driver_remove - deinitialize hw power domain state
 * @i915: i915 device instance
 *
 * De-initializes the display power domain HW state. It also ensures that the
 * device stays powered up so that the driver can be reloaded.
 *
 * It must be called with power domains already disabled (after a call to
 * intel_power_domains_disable()) and must be paired with
 * intel_power_domains_init_hw().
 */
void intel_power_domains_driver_remove(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&i915->power_domains.wakeref);

	/* Remove the refcount we took to keep power well support disabled. */
	if (!i915->params.disable_power_well)
		intel_display_power_put_unchecked(i915, POWER_DOMAIN_INIT);

	intel_display_power_flush_work_sync(i915);

	intel_power_domains_verify_state(i915);

	/* Keep the power well enabled, but cancel its rpm wakeref. */
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
}

/**
 * intel_power_domains_enable - enable toggling of display power wells
 * @i915: i915 device instance
 *
 * Enable the ondemand enabling/disabling of the display power wells. Note that
 * power wells not belonging to POWER_DOMAIN_INIT are allowed to be toggled
 * only at specific points of the display modeset sequence, thus they are not
 * affected by the intel_power_domains_enable()/disable() calls. The purpose
 * of these function is to keep the rest of power wells enabled until the end
 * of display HW readout (which will acquire the power references reflecting
 * the current HW state).
 */
void intel_power_domains_enable(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&i915->power_domains.wakeref);

	intel_display_power_put(i915, POWER_DOMAIN_INIT, wakeref);
	intel_power_domains_verify_state(i915);
}

/**
 * intel_power_domains_disable - disable toggling of display power wells
 * @i915: i915 device instance
 *
 * Disable the ondemand enabling/disabling of the display power wells. See
 * intel_power_domains_enable() for which power wells this call controls.
 */
void intel_power_domains_disable(struct drm_i915_private *i915)
{
	struct i915_power_domains *power_domains = &i915->power_domains;

	drm_WARN_ON(&i915->drm, power_domains->wakeref);
	power_domains->wakeref =
		intel_display_power_get(i915, POWER_DOMAIN_INIT);

	intel_power_domains_verify_state(i915);
}

/**
 * intel_power_domains_suspend - suspend power domain state
 * @i915: i915 device instance
 * @suspend_mode: specifies the target suspend state (idle, mem, hibernation)
 *
 * This function prepares the hardware power domain state before entering
 * system suspend.
 *
 * It must be called with power domains already disabled (after a call to
 * intel_power_domains_disable()) and paired with intel_power_domains_resume().
 */
void intel_power_domains_suspend(struct drm_i915_private *i915,
				 enum i915_drm_suspend_mode suspend_mode)
{
	struct i915_power_domains *power_domains = &i915->power_domains;
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&power_domains->wakeref);

	intel_display_power_put(i915, POWER_DOMAIN_INIT, wakeref);

	/*
	 * In case of suspend-to-idle (aka S0ix) on a DMC platform without DC9
	 * support don't manually deinit the power domains. This also means the
	 * CSR/DMC firmware will stay active, it will power down any HW
	 * resources as required and also enable deeper system power states
	 * that would be blocked if the firmware was inactive.
	 */
	if (!(i915->csr.allowed_dc_mask & DC_STATE_EN_DC9) &&
	    suspend_mode == I915_DRM_SUSPEND_IDLE &&
	    i915->csr.dmc_payload) {
		intel_display_power_flush_work(i915);
		intel_power_domains_verify_state(i915);
		return;
	}

	/*
	 * Even if power well support was disabled we still want to disable
	 * power wells if power domains must be deinitialized for suspend.
	 */
	if (!i915->params.disable_power_well)
		intel_display_power_put_unchecked(i915, POWER_DOMAIN_INIT);

	intel_display_power_flush_work(i915);
	intel_power_domains_verify_state(i915);

	if (INTEL_GEN(i915) >= 11)
		icl_display_core_uninit(i915);
	else if (IS_CANNONLAKE(i915))
		cnl_display_core_uninit(i915);
	else if (IS_GEN9_BC(i915))
		skl_display_core_uninit(i915);
	else if (IS_GEN9_LP(i915))
		bxt_display_core_uninit(i915);

	power_domains->display_core_suspended = true;
}

/**
 * intel_power_domains_resume - resume power domain state
 * @i915: i915 device instance
 *
 * This function resume the hardware power domain state during system resume.
 *
 * It will return with power domain support disabled (to be enabled later by
 * intel_power_domains_enable()) and must be paired with
 * intel_power_domains_suspend().
 */
void intel_power_domains_resume(struct drm_i915_private *i915)
{
	struct i915_power_domains *power_domains = &i915->power_domains;

	if (power_domains->display_core_suspended) {
		intel_power_domains_init_hw(i915, true);
		power_domains->display_core_suspended = false;
	} else {
		drm_WARN_ON(&i915->drm, power_domains->wakeref);
		power_domains->wakeref =
			intel_display_power_get(i915, POWER_DOMAIN_INIT);
	}

	intel_power_domains_verify_state(i915);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)

static void intel_power_domains_dump_info(struct drm_i915_private *i915)
{
	struct i915_power_domains *power_domains = &i915->power_domains;
	struct i915_power_well *power_well;

	for_each_power_well(i915, power_well) {
		enum intel_display_power_domain domain;

		drm_dbg(&i915->drm, "%-25s %d\n",
			power_well->desc->name, power_well->count);

		for_each_power_domain(domain, power_well->desc->domains)
			drm_dbg(&i915->drm, "  %-23s %d\n",
				intel_display_power_domain_str(domain),
				power_domains->domain_use_count[domain]);
	}
}

/**
 * intel_power_domains_verify_state - verify the HW/SW state for all power wells
 * @i915: i915 device instance
 *
 * Verify if the reference count of each power well matches its HW enabled
 * state and the total refcount of the domains it belongs to. This must be
 * called after modeset HW state sanitization, which is responsible for
 * acquiring reference counts for any power wells in use and disabling the
 * ones left on by BIOS but not required by any active output.
 */
static void intel_power_domains_verify_state(struct drm_i915_private *i915)
{
	struct i915_power_domains *power_domains = &i915->power_domains;
	struct i915_power_well *power_well;
	bool dump_domain_info;

	mutex_lock(&power_domains->lock);

	verify_async_put_domains_state(power_domains);

	dump_domain_info = false;
	for_each_power_well(i915, power_well) {
		enum intel_display_power_domain domain;
		int domains_count;
		bool enabled;

		enabled = power_well->desc->ops->is_enabled(i915, power_well);
		if ((power_well->count || power_well->desc->always_on) !=
		    enabled)
			drm_err(&i915->drm,
				"power well %s state mismatch (refcount %d/enabled %d)",
				power_well->desc->name,
				power_well->count, enabled);

		domains_count = 0;
		for_each_power_domain(domain, power_well->desc->domains)
			domains_count += power_domains->domain_use_count[domain];

		if (power_well->count != domains_count) {
			drm_err(&i915->drm,
				"power well %s refcount/domain refcount mismatch "
				"(refcount %d/domains refcount %d)\n",
				power_well->desc->name, power_well->count,
				domains_count);
			dump_domain_info = true;
		}
	}

	if (dump_domain_info) {
		static bool dumped;

		if (!dumped) {
			intel_power_domains_dump_info(i915);
			dumped = true;
		}
	}

	mutex_unlock(&power_domains->lock);
}

#else

static void intel_power_domains_verify_state(struct drm_i915_private *i915)
{
}

#endif

void intel_display_power_suspend_late(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) >= 11 || IS_GEN9_LP(i915))
		bxt_enable_dc9(i915);
	else if (IS_HASWELL(i915) || IS_BROADWELL(i915))
		hsw_enable_pc8(i915);
}

void intel_display_power_resume_early(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) >= 11 || IS_GEN9_LP(i915)) {
		gen9_sanitize_dc_state(i915);
		bxt_disable_dc9(i915);
	} else if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
		hsw_disable_pc8(i915);
	}
}

void intel_display_power_suspend(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) >= 11) {
		icl_display_core_uninit(i915);
		bxt_enable_dc9(i915);
	} else if (IS_GEN9_LP(i915)) {
		bxt_display_core_uninit(i915);
		bxt_enable_dc9(i915);
	} else if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
		hsw_enable_pc8(i915);
	}
}

void intel_display_power_resume(struct drm_i915_private *i915)
{
	if (INTEL_GEN(i915) >= 11) {
		bxt_disable_dc9(i915);
		icl_display_core_init(i915, true);
		if (i915->csr.dmc_payload) {
			if (i915->csr.allowed_dc_mask &
			    DC_STATE_EN_UPTO_DC6)
				skl_enable_dc6(i915);
			else if (i915->csr.allowed_dc_mask &
				 DC_STATE_EN_UPTO_DC5)
				gen9_enable_dc5(i915);
		}
	} else if (IS_GEN9_LP(i915)) {
		bxt_disable_dc9(i915);
		bxt_display_core_init(i915, true);
		if (i915->csr.dmc_payload &&
		    (i915->csr.allowed_dc_mask & DC_STATE_EN_UPTO_DC5))
			gen9_enable_dc5(i915);
	} else if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
		hsw_disable_pc8(i915);
	}
}
