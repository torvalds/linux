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

#include <drm/drm_print.h>

#include "i915_drv.h"
#include "intel_cdclk.h"
#include "intel_crt.h"
#include "intel_csr.h"
#include "intel_dp.h"
#include "intel_drv.h"

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

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)

#include <linux/sort.h>

#define STACKDEPTH 8

static noinline depot_stack_handle_t __save_depot_stack(void)
{
	unsigned long entries[STACKDEPTH];
	struct stack_trace trace = {
		.entries = entries,
		.max_entries = ARRAY_SIZE(entries),
		.skip = 1,
	};

	save_stack_trace(&trace);
	if (trace.nr_entries &&
	    trace.entries[trace.nr_entries - 1] == ULONG_MAX)
		trace.nr_entries--;

	return depot_save_stack(&trace, GFP_NOWAIT | __GFP_NOWARN);
}

static void __print_depot_stack(depot_stack_handle_t stack,
				char *buf, int sz, int indent)
{
	unsigned long entries[STACKDEPTH];
	struct stack_trace trace = {
		.entries = entries,
		.max_entries = ARRAY_SIZE(entries),
	};

	depot_fetch_stack(stack, &trace);
	snprint_stack_trace(buf, sz, &trace, indent);
}

static void init_intel_runtime_pm_wakeref(struct drm_i915_private *i915)
{
	struct i915_runtime_pm *rpm = &i915->runtime_pm;

	spin_lock_init(&rpm->debug.lock);
}

static noinline depot_stack_handle_t
track_intel_runtime_pm_wakeref(struct drm_i915_private *i915)
{
	struct i915_runtime_pm *rpm = &i915->runtime_pm;
	depot_stack_handle_t stack, *stacks;
	unsigned long flags;

	atomic_inc(&rpm->wakeref_count);
	assert_rpm_wakelock_held(i915);

	if (!HAS_RUNTIME_PM(i915))
		return -1;

	stack = __save_depot_stack();
	if (!stack)
		return -1;

	spin_lock_irqsave(&rpm->debug.lock, flags);

	if (!rpm->debug.count)
		rpm->debug.last_acquire = stack;

	stacks = krealloc(rpm->debug.owners,
			  (rpm->debug.count + 1) * sizeof(*stacks),
			  GFP_NOWAIT | __GFP_NOWARN);
	if (stacks) {
		stacks[rpm->debug.count++] = stack;
		rpm->debug.owners = stacks;
	} else {
		stack = -1;
	}

	spin_unlock_irqrestore(&rpm->debug.lock, flags);

	return stack;
}

static void cancel_intel_runtime_pm_wakeref(struct drm_i915_private *i915,
					    depot_stack_handle_t stack)
{
	struct i915_runtime_pm *rpm = &i915->runtime_pm;
	unsigned long flags, n;
	bool found = false;

	if (unlikely(stack == -1))
		return;

	spin_lock_irqsave(&rpm->debug.lock, flags);
	for (n = rpm->debug.count; n--; ) {
		if (rpm->debug.owners[n] == stack) {
			memmove(rpm->debug.owners + n,
				rpm->debug.owners + n + 1,
				(--rpm->debug.count - n) * sizeof(stack));
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&rpm->debug.lock, flags);

	if (WARN(!found,
		 "Unmatched wakeref (tracking %lu), count %u\n",
		 rpm->debug.count, atomic_read(&rpm->wakeref_count))) {
		char *buf;

		buf = kmalloc(PAGE_SIZE, GFP_NOWAIT | __GFP_NOWARN);
		if (!buf)
			return;

		__print_depot_stack(stack, buf, PAGE_SIZE, 2);
		DRM_DEBUG_DRIVER("wakeref %x from\n%s", stack, buf);

		stack = READ_ONCE(rpm->debug.last_release);
		if (stack) {
			__print_depot_stack(stack, buf, PAGE_SIZE, 2);
			DRM_DEBUG_DRIVER("wakeref last released at\n%s", buf);
		}

		kfree(buf);
	}
}

static int cmphandle(const void *_a, const void *_b)
{
	const depot_stack_handle_t * const a = _a, * const b = _b;

	if (*a < *b)
		return -1;
	else if (*a > *b)
		return 1;
	else
		return 0;
}

static void
__print_intel_runtime_pm_wakeref(struct drm_printer *p,
				 const struct intel_runtime_pm_debug *dbg)
{
	unsigned long i;
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_NOWAIT | __GFP_NOWARN);
	if (!buf)
		return;

	if (dbg->last_acquire) {
		__print_depot_stack(dbg->last_acquire, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref last acquired:\n%s", buf);
	}

	if (dbg->last_release) {
		__print_depot_stack(dbg->last_release, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref last released:\n%s", buf);
	}

	drm_printf(p, "Wakeref count: %lu\n", dbg->count);

	sort(dbg->owners, dbg->count, sizeof(*dbg->owners), cmphandle, NULL);

	for (i = 0; i < dbg->count; i++) {
		depot_stack_handle_t stack = dbg->owners[i];
		unsigned long rep;

		rep = 1;
		while (i + 1 < dbg->count && dbg->owners[i + 1] == stack)
			rep++, i++;
		__print_depot_stack(stack, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref x%lu taken at:\n%s", rep, buf);
	}

	kfree(buf);
}

static noinline void
untrack_intel_runtime_pm_wakeref(struct drm_i915_private *i915)
{
	struct i915_runtime_pm *rpm = &i915->runtime_pm;
	struct intel_runtime_pm_debug dbg = {};
	struct drm_printer p;
	unsigned long flags;

	assert_rpm_wakelock_held(i915);
	if (atomic_dec_and_lock_irqsave(&rpm->wakeref_count,
					&rpm->debug.lock,
					flags)) {
		dbg = rpm->debug;

		rpm->debug.owners = NULL;
		rpm->debug.count = 0;
		rpm->debug.last_release = __save_depot_stack();

		spin_unlock_irqrestore(&rpm->debug.lock, flags);
	}
	if (!dbg.count)
		return;

	p = drm_debug_printer("i915");
	__print_intel_runtime_pm_wakeref(&p, &dbg);

	kfree(dbg.owners);
}

void print_intel_runtime_pm_wakeref(struct drm_i915_private *i915,
				    struct drm_printer *p)
{
	struct intel_runtime_pm_debug dbg = {};

	do {
		struct i915_runtime_pm *rpm = &i915->runtime_pm;
		unsigned long alloc = dbg.count;
		depot_stack_handle_t *s;

		spin_lock_irq(&rpm->debug.lock);
		dbg.count = rpm->debug.count;
		if (dbg.count <= alloc) {
			memcpy(dbg.owners,
			       rpm->debug.owners,
			       dbg.count * sizeof(*s));
		}
		dbg.last_acquire = rpm->debug.last_acquire;
		dbg.last_release = rpm->debug.last_release;
		spin_unlock_irq(&rpm->debug.lock);
		if (dbg.count <= alloc)
			break;

		s = krealloc(dbg.owners,
			     dbg.count * sizeof(*s),
			     GFP_NOWAIT | __GFP_NOWARN);
		if (!s)
			goto out;

		dbg.owners = s;
	} while (1);

	__print_intel_runtime_pm_wakeref(p, &dbg);

out:
	kfree(dbg.owners);
}

#else

static void init_intel_runtime_pm_wakeref(struct drm_i915_private *i915)
{
}

static depot_stack_handle_t
track_intel_runtime_pm_wakeref(struct drm_i915_private *i915)
{
	atomic_inc(&i915->runtime_pm.wakeref_count);
	assert_rpm_wakelock_held(i915);
	return -1;
}

static void untrack_intel_runtime_pm_wakeref(struct drm_i915_private *i915)
{
	assert_rpm_wakelock_held(i915);
	atomic_dec(&i915->runtime_pm.wakeref_count);
}

#endif

bool intel_display_power_well_is_enabled(struct drm_i915_private *dev_priv,
					 enum i915_power_well_id power_well_id);

const char *
intel_display_power_domain_str(enum intel_display_power_domain domain)
{
	switch (domain) {
	case POWER_DOMAIN_PIPE_A:
		return "PIPE_A";
	case POWER_DOMAIN_PIPE_B:
		return "PIPE_B";
	case POWER_DOMAIN_PIPE_C:
		return "PIPE_C";
	case POWER_DOMAIN_PIPE_A_PANEL_FITTER:
		return "PIPE_A_PANEL_FITTER";
	case POWER_DOMAIN_PIPE_B_PANEL_FITTER:
		return "PIPE_B_PANEL_FITTER";
	case POWER_DOMAIN_PIPE_C_PANEL_FITTER:
		return "PIPE_C_PANEL_FITTER";
	case POWER_DOMAIN_TRANSCODER_A:
		return "TRANSCODER_A";
	case POWER_DOMAIN_TRANSCODER_B:
		return "TRANSCODER_B";
	case POWER_DOMAIN_TRANSCODER_C:
		return "TRANSCODER_C";
	case POWER_DOMAIN_TRANSCODER_EDP:
		return "TRANSCODER_EDP";
	case POWER_DOMAIN_TRANSCODER_EDP_VDSC:
		return "TRANSCODER_EDP_VDSC";
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
	case POWER_DOMAIN_PLLS:
		return "PLLS";
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
	case POWER_DOMAIN_AUX_IO_A:
		return "AUX_IO_A";
	case POWER_DOMAIN_AUX_TBT1:
		return "AUX_TBT1";
	case POWER_DOMAIN_AUX_TBT2:
		return "AUX_TBT2";
	case POWER_DOMAIN_AUX_TBT3:
		return "AUX_TBT3";
	case POWER_DOMAIN_AUX_TBT4:
		return "AUX_TBT4";
	case POWER_DOMAIN_GMBUS:
		return "GMBUS";
	case POWER_DOMAIN_INIT:
		return "INIT";
	case POWER_DOMAIN_MODESET:
		return "MODESET";
	case POWER_DOMAIN_GT_IRQ:
		return "GT_IRQ";
	default:
		MISSING_CASE(domain);
		return "?";
	}
}

static void intel_power_well_enable(struct drm_i915_private *dev_priv,
				    struct i915_power_well *power_well)
{
	DRM_DEBUG_KMS("enabling %s\n", power_well->desc->name);
	power_well->desc->ops->enable(dev_priv, power_well);
	power_well->hw_enabled = true;
}

static void intel_power_well_disable(struct drm_i915_private *dev_priv,
				     struct i915_power_well *power_well)
{
	DRM_DEBUG_KMS("disabling %s\n", power_well->desc->name);
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
	WARN(!power_well->count, "Use count on power well %s is already zero",
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
	struct pci_dev *pdev = dev_priv->drm.pdev;

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
	if (has_vga) {
		vga_get_uninterruptible(pdev, VGA_RSRC_LEGACY_IO);
		outb(inb(VGA_MSR_READ), VGA_MSR_WRITE);
		vga_put(pdev, VGA_RSRC_LEGACY_IO);
	}

	if (irq_pipe_mask)
		gen8_irq_power_well_post_enable(dev_priv, irq_pipe_mask);
}

static void hsw_power_well_pre_disable(struct drm_i915_private *dev_priv,
				       u8 irq_pipe_mask)
{
	if (irq_pipe_mask)
		gen8_irq_power_well_pre_disable(dev_priv, irq_pipe_mask);
}


static void hsw_wait_for_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;

	/* Timeout for PW1:10 us, AUX:not specified, other PWs:20 us. */
	WARN_ON(intel_wait_for_register(&dev_priv->uncore,
					regs->driver,
					HSW_PWR_WELL_CTL_STATE(pw_idx),
					HSW_PWR_WELL_CTL_STATE(pw_idx),
					1));
}

static u32 hsw_power_well_requesters(struct drm_i915_private *dev_priv,
				     const struct i915_power_well_regs *regs,
				     int pw_idx)
{
	u32 req_mask = HSW_PWR_WELL_CTL_REQ(pw_idx);
	u32 ret;

	ret = I915_READ(regs->bios) & req_mask ? 1 : 0;
	ret |= I915_READ(regs->driver) & req_mask ? 2 : 0;
	if (regs->kvmr.reg)
		ret |= I915_READ(regs->kvmr) & req_mask ? 4 : 0;
	ret |= I915_READ(regs->debug) & req_mask ? 8 : 0;

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
	wait_for((disabled = !(I915_READ(regs->driver) &
			       HSW_PWR_WELL_CTL_STATE(pw_idx))) ||
		 (reqs = hsw_power_well_requesters(dev_priv, regs, pw_idx)), 1);
	if (disabled)
		return;

	DRM_DEBUG_KMS("%s forced on (bios:%d driver:%d kvmr:%d debug:%d)\n",
		      power_well->desc->name,
		      !!(reqs & 1), !!(reqs & 2), !!(reqs & 4), !!(reqs & 8));
}

static void gen9_wait_for_power_well_fuses(struct drm_i915_private *dev_priv,
					   enum skl_power_gate pg)
{
	/* Timeout 5us for PG#0, for other PGs 1us */
	WARN_ON(intel_wait_for_register(&dev_priv->uncore, SKL_FUSE_STATUS,
					SKL_FUSE_PG_DIST_STATUS(pg),
					SKL_FUSE_PG_DIST_STATUS(pg), 1));
}

static void hsw_power_well_enable(struct drm_i915_private *dev_priv,
				  struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	bool wait_fuses = power_well->desc->hsw.has_fuses;
	enum skl_power_gate uninitialized_var(pg);
	u32 val;

	if (wait_fuses) {
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

	val = I915_READ(regs->driver);
	I915_WRITE(regs->driver, val | HSW_PWR_WELL_CTL_REQ(pw_idx));
	hsw_wait_for_power_well_enable(dev_priv, power_well);

	/* Display WA #1178: cnl */
	if (IS_CANNONLAKE(dev_priv) &&
	    pw_idx >= GLK_PW_CTL_IDX_AUX_B &&
	    pw_idx <= CNL_PW_CTL_IDX_AUX_F) {
		val = I915_READ(CNL_AUX_ANAOVRD1(pw_idx));
		val |= CNL_AUX_ANAOVRD1_ENABLE | CNL_AUX_ANAOVRD1_LDO_BYPASS;
		I915_WRITE(CNL_AUX_ANAOVRD1(pw_idx), val);
	}

	if (wait_fuses)
		gen9_wait_for_power_well_fuses(dev_priv, pg);

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

	val = I915_READ(regs->driver);
	I915_WRITE(regs->driver, val & ~HSW_PWR_WELL_CTL_REQ(pw_idx));
	hsw_wait_for_power_well_disable(dev_priv, power_well);
}

#define ICL_AUX_PW_TO_PORT(pw_idx)	((pw_idx) - ICL_PW_CTL_IDX_AUX_A)

static void
icl_combo_phy_aux_power_well_enable(struct drm_i915_private *dev_priv,
				    struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	enum port port = ICL_AUX_PW_TO_PORT(pw_idx);
	u32 val;

	val = I915_READ(regs->driver);
	I915_WRITE(regs->driver, val | HSW_PWR_WELL_CTL_REQ(pw_idx));

	val = I915_READ(ICL_PORT_CL_DW12(port));
	I915_WRITE(ICL_PORT_CL_DW12(port), val | ICL_LANE_ENABLE_AUX);

	hsw_wait_for_power_well_enable(dev_priv, power_well);

	/* Display WA #1178: icl */
	if (IS_ICELAKE(dev_priv) &&
	    pw_idx >= ICL_PW_CTL_IDX_AUX_A && pw_idx <= ICL_PW_CTL_IDX_AUX_B &&
	    !intel_bios_is_port_edp(dev_priv, port)) {
		val = I915_READ(ICL_AUX_ANAOVRD1(pw_idx));
		val |= ICL_AUX_ANAOVRD1_ENABLE | ICL_AUX_ANAOVRD1_LDO_BYPASS;
		I915_WRITE(ICL_AUX_ANAOVRD1(pw_idx), val);
	}
}

static void
icl_combo_phy_aux_power_well_disable(struct drm_i915_private *dev_priv,
				     struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	enum port port = ICL_AUX_PW_TO_PORT(pw_idx);
	u32 val;

	val = I915_READ(ICL_PORT_CL_DW12(port));
	I915_WRITE(ICL_PORT_CL_DW12(port), val & ~ICL_LANE_ENABLE_AUX);

	val = I915_READ(regs->driver);
	I915_WRITE(regs->driver, val & ~HSW_PWR_WELL_CTL_REQ(pw_idx));

	hsw_wait_for_power_well_disable(dev_priv, power_well);
}

#define ICL_AUX_PW_TO_CH(pw_idx)	\
	((pw_idx) - ICL_PW_CTL_IDX_AUX_A + AUX_CH_A)

static void
icl_tc_phy_aux_power_well_enable(struct drm_i915_private *dev_priv,
				 struct i915_power_well *power_well)
{
	enum aux_ch aux_ch = ICL_AUX_PW_TO_CH(power_well->desc->hsw.idx);
	u32 val;

	val = I915_READ(DP_AUX_CH_CTL(aux_ch));
	val &= ~DP_AUX_CH_CTL_TBT_IO;
	if (power_well->desc->hsw.is_tc_tbt)
		val |= DP_AUX_CH_CTL_TBT_IO;
	I915_WRITE(DP_AUX_CH_CTL(aux_ch), val);

	hsw_power_well_enable(dev_priv, power_well);
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

	val = I915_READ(regs->driver);

	/*
	 * On GEN9 big core due to a DMC bug the driver's request bits for PW1
	 * and the MISC_IO PW will be not restored, so check instead for the
	 * BIOS's own request bits, which are forced-on for these power wells
	 * when exiting DC5/6.
	 */
	if (IS_GEN(dev_priv, 9) && !IS_GEN9_LP(dev_priv) &&
	    (id == SKL_DISP_PW_1 || id == SKL_DISP_PW_MISC_IO))
		val |= I915_READ(regs->bios);

	return (val & mask) == mask;
}

static void assert_can_enable_dc9(struct drm_i915_private *dev_priv)
{
	WARN_ONCE((I915_READ(DC_STATE_EN) & DC_STATE_EN_DC9),
		  "DC9 already programmed to be enabled.\n");
	WARN_ONCE(I915_READ(DC_STATE_EN) & DC_STATE_EN_UPTO_DC5,
		  "DC5 still not disabled to enable DC9.\n");
	WARN_ONCE(I915_READ(HSW_PWR_WELL_CTL2) &
		  HSW_PWR_WELL_CTL_REQ(SKL_PW_CTL_IDX_PW_2),
		  "Power well 2 on.\n");
	WARN_ONCE(intel_irqs_enabled(dev_priv),
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
	WARN_ONCE(intel_irqs_enabled(dev_priv),
		  "Interrupts not disabled yet.\n");
	WARN_ONCE(I915_READ(DC_STATE_EN) & DC_STATE_EN_UPTO_DC5,
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

	I915_WRITE(DC_STATE_EN, state);

	/* It has been observed that disabling the dc6 state sometimes
	 * doesn't stick and dmc keeps returning old value. Make sure
	 * the write really sticks enough times and also force rewrite until
	 * we are confident that state is exactly what we want.
	 */
	do  {
		v = I915_READ(DC_STATE_EN);

		if (v != state) {
			I915_WRITE(DC_STATE_EN, state);
			rewrites++;
			rereads = 0;
		} else if (rereads++ > 5) {
			break;
		}

	} while (rewrites < 100);

	if (v != state)
		DRM_ERROR("Writing dc state to 0x%x failed, now 0x%x\n",
			  state, v);

	/* Most of the times we need one retry, avoid spam */
	if (rewrites > 1)
		DRM_DEBUG_KMS("Rewrote dc state to 0x%x %d times\n",
			      state, rewrites);
}

static u32 gen9_dc_mask(struct drm_i915_private *dev_priv)
{
	u32 mask;

	mask = DC_STATE_EN_UPTO_DC5;
	if (INTEL_GEN(dev_priv) >= 11)
		mask |= DC_STATE_EN_UPTO_DC6 | DC_STATE_EN_DC9;
	else if (IS_GEN9_LP(dev_priv))
		mask |= DC_STATE_EN_DC9;
	else
		mask |= DC_STATE_EN_UPTO_DC6;

	return mask;
}

void gen9_sanitize_dc_state(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = I915_READ(DC_STATE_EN) & gen9_dc_mask(dev_priv);

	DRM_DEBUG_KMS("Resetting DC state tracking from %02x to %02x\n",
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

	if (WARN_ON_ONCE(state & ~dev_priv->csr.allowed_dc_mask))
		state &= dev_priv->csr.allowed_dc_mask;

	val = I915_READ(DC_STATE_EN);
	mask = gen9_dc_mask(dev_priv);
	DRM_DEBUG_KMS("Setting DC state from %02x to %02x\n",
		      val & mask, state);

	/* Check if DMC is ignoring our DC state requests */
	if ((val & mask) != dev_priv->csr.dc_state)
		DRM_ERROR("DC state mismatch (0x%x -> 0x%x)\n",
			  dev_priv->csr.dc_state, val & mask);

	val &= ~mask;
	val |= state;

	gen9_write_dc_state(dev_priv, val);

	dev_priv->csr.dc_state = val & mask;
}

void bxt_enable_dc9(struct drm_i915_private *dev_priv)
{
	assert_can_enable_dc9(dev_priv);

	DRM_DEBUG_KMS("Enabling DC9\n");
	/*
	 * Power sequencer reset is not needed on
	 * platforms with South Display Engine on PCH,
	 * because PPS registers are always on.
	 */
	if (!HAS_PCH_SPLIT(dev_priv))
		intel_power_sequencer_reset(dev_priv);
	gen9_set_dc_state(dev_priv, DC_STATE_EN_DC9);
}

void bxt_disable_dc9(struct drm_i915_private *dev_priv)
{
	assert_can_disable_dc9(dev_priv);

	DRM_DEBUG_KMS("Disabling DC9\n");

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	intel_pps_unlock_regs_wa(dev_priv);
}

static void assert_csr_loaded(struct drm_i915_private *dev_priv)
{
	WARN_ONCE(!I915_READ(CSR_PROGRAM(0)),
		  "CSR program storage start is NULL\n");
	WARN_ONCE(!I915_READ(CSR_SSP_BASE), "CSR SSP Base Not fine\n");
	WARN_ONCE(!I915_READ(CSR_HTP_SKL), "CSR HTP Not fine\n");
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
	WARN(1, "Power well %d not defined for this platform\n", power_well_id);
	return &dev_priv->power_domains.power_wells[0];
}

static void assert_can_enable_dc5(struct drm_i915_private *dev_priv)
{
	bool pg2_enabled = intel_display_power_well_is_enabled(dev_priv,
					SKL_DISP_PW_2);

	WARN_ONCE(pg2_enabled, "PG2 not disabled to enable DC5.\n");

	WARN_ONCE((I915_READ(DC_STATE_EN) & DC_STATE_EN_UPTO_DC5),
		  "DC5 already programmed to be enabled.\n");
	assert_rpm_wakelock_held(dev_priv);

	assert_csr_loaded(dev_priv);
}

void gen9_enable_dc5(struct drm_i915_private *dev_priv)
{
	assert_can_enable_dc5(dev_priv);

	DRM_DEBUG_KMS("Enabling DC5\n");

	/* Wa Display #1183: skl,kbl,cfl */
	if (IS_GEN9_BC(dev_priv))
		I915_WRITE(GEN8_CHICKEN_DCPR_1, I915_READ(GEN8_CHICKEN_DCPR_1) |
			   SKL_SELECT_ALTERNATE_DC_EXIT);

	gen9_set_dc_state(dev_priv, DC_STATE_EN_UPTO_DC5);
}

static void assert_can_enable_dc6(struct drm_i915_private *dev_priv)
{
	WARN_ONCE(I915_READ(UTIL_PIN_CTL) & UTIL_PIN_ENABLE,
		  "Backlight is not disabled.\n");
	WARN_ONCE((I915_READ(DC_STATE_EN) & DC_STATE_EN_UPTO_DC6),
		  "DC6 already programmed to be enabled.\n");

	assert_csr_loaded(dev_priv);
}

void skl_enable_dc6(struct drm_i915_private *dev_priv)
{
	assert_can_enable_dc6(dev_priv);

	DRM_DEBUG_KMS("Enabling DC6\n");

	/* Wa Display #1183: skl,kbl,cfl */
	if (IS_GEN9_BC(dev_priv))
		I915_WRITE(GEN8_CHICKEN_DCPR_1, I915_READ(GEN8_CHICKEN_DCPR_1) |
			   SKL_SELECT_ALTERNATE_DC_EXIT);

	gen9_set_dc_state(dev_priv, DC_STATE_EN_UPTO_DC6);
}

static void hsw_power_well_sync_hw(struct drm_i915_private *dev_priv,
				   struct i915_power_well *power_well)
{
	const struct i915_power_well_regs *regs = power_well->desc->hsw.regs;
	int pw_idx = power_well->desc->hsw.idx;
	u32 mask = HSW_PWR_WELL_CTL_REQ(pw_idx);
	u32 bios_req = I915_READ(regs->bios);

	/* Take over the request bit if set by BIOS. */
	if (bios_req & mask) {
		u32 drv_req = I915_READ(regs->driver);

		if (!(drv_req & mask))
			I915_WRITE(regs->driver, drv_req | mask);
		I915_WRITE(regs->bios, bios_req & ~mask);
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
	return (I915_READ(DC_STATE_EN) & DC_STATE_EN_UPTO_DC5_DC6_MASK) == 0;
}

static void gen9_assert_dbuf_enabled(struct drm_i915_private *dev_priv)
{
	u32 tmp = I915_READ(DBUF_CTL);

	WARN((tmp & (DBUF_POWER_STATE | DBUF_POWER_REQUEST)) !=
	     (DBUF_POWER_STATE | DBUF_POWER_REQUEST),
	     "Unexpected DBuf power power state (0x%08x)\n", tmp);
}

static void gen9_dc_off_power_well_enable(struct drm_i915_private *dev_priv,
					  struct i915_power_well *power_well)
{
	struct intel_cdclk_state cdclk_state = {};

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	dev_priv->display.get_cdclk(dev_priv, &cdclk_state);
	/* Can't read out voltage_level so can't use intel_cdclk_changed() */
	WARN_ON(intel_cdclk_needs_modeset(&dev_priv->cdclk.hw, &cdclk_state));

	gen9_assert_dbuf_enabled(dev_priv);

	if (IS_GEN9_LP(dev_priv))
		bxt_verify_ddi_phy_power_wells(dev_priv);

	if (INTEL_GEN(dev_priv) >= 11)
		/*
		 * DMC retains HW context only for port A, the other combo
		 * PHY's HW context for port B is lost after DC transitions,
		 * so we need to restore it manually.
		 */
		icl_combo_phys_init(dev_priv);
}

static void gen9_dc_off_power_well_disable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	if (!dev_priv->csr.dmc_payload)
		return;

	if (dev_priv->csr.allowed_dc_mask & DC_STATE_EN_UPTO_DC6)
		skl_enable_dc6(dev_priv);
	else if (dev_priv->csr.allowed_dc_mask & DC_STATE_EN_UPTO_DC5)
		gen9_enable_dc5(dev_priv);
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
	if ((I915_READ(PIPECONF(PIPE_A)) & PIPECONF_ENABLE) == 0)
		i830_enable_pipe(dev_priv, PIPE_A);
	if ((I915_READ(PIPECONF(PIPE_B)) & PIPECONF_ENABLE) == 0)
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
	return I915_READ(PIPECONF(PIPE_A)) & PIPECONF_ENABLE &&
		I915_READ(PIPECONF(PIPE_B)) & PIPECONF_ENABLE;
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

	mutex_lock(&dev_priv->pcu_lock);

#define COND \
	((vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL);
	ctrl &= ~mask;
	ctrl |= state;
	vlv_punit_write(dev_priv, PUNIT_REG_PWRGT_CTRL, ctrl);

	if (wait_for(COND, 100))
		DRM_ERROR("timeout setting power well state %08x (%08x)\n",
			  state,
			  vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL));

#undef COND

out:
	mutex_unlock(&dev_priv->pcu_lock);
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

	mutex_lock(&dev_priv->pcu_lock);

	state = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS) & mask;
	/*
	 * We only ever set the power-on and power-gate states, anything
	 * else is unexpected.
	 */
	WARN_ON(state != PUNIT_PWRGT_PWR_ON(pw_idx) &&
		state != PUNIT_PWRGT_PWR_GATE(pw_idx));
	if (state == ctrl)
		enabled = true;

	/*
	 * A transient state at this point would mean some unexpected party
	 * is poking at the power controls too.
	 */
	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_CTRL) & mask;
	WARN_ON(ctrl != state);

	mutex_unlock(&dev_priv->pcu_lock);

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
	val = I915_READ(DSPCLK_GATE_D);
	val &= DPOUNIT_CLOCK_GATE_DISABLE;
	val |= VRHUNIT_CLOCK_GATE_DISABLE;
	I915_WRITE(DSPCLK_GATE_D, val);

	/*
	 * Disable trickle feed and enable pnd deadline calculation
	 */
	I915_WRITE(MI_ARB_VLV, MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE);
	I915_WRITE(CBR1_VLV, 0);

	WARN_ON(dev_priv->rawclk_freq == 0);

	I915_WRITE(RAWCLK_FREQ_VLV,
		   DIV_ROUND_CLOSEST(dev_priv->rawclk_freq, 1000));
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
		u32 val = I915_READ(DPLL(pipe));

		val |= DPLL_REF_CLK_ENABLE_VLV | DPLL_VGA_MODE_DIS;
		if (pipe != PIPE_A)
			val |= DPLL_INTEGRATED_CRI_CLK_VLV;

		I915_WRITE(DPLL(pipe), val);
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

	/* Re-enable the ADPA, if we have one */
	for_each_intel_encoder(&dev_priv->drm, encoder) {
		if (encoder->type == INTEL_OUTPUT_ANALOG)
			intel_crt_reset(&encoder->base);
	}

	i915_redisable_vga_power_on(dev_priv);

	intel_pps_unlock_regs_wa(dev_priv);
}

static void vlv_display_power_well_deinit(struct drm_i915_private *dev_priv)
{
	spin_lock_irq(&dev_priv->irq_lock);
	valleyview_disable_display_irqs(dev_priv);
	spin_unlock_irq(&dev_priv->irq_lock);

	/* make sure we're done processing display irqs */
	synchronize_irq(dev_priv->drm.irq);

	intel_power_sequencer_reset(dev_priv);

	/* Prevent us from re-enabling polling on accident in late suspend */
	if (!dev_priv->drm.dev->power.is_suspended)
		intel_hpd_poll_init(dev_priv);
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
	I915_WRITE(DPIO_CTL, I915_READ(DPIO_CTL) | DPIO_CMNRST);
}

static void vlv_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		assert_pll_disabled(dev_priv, pipe);

	/* Assert common reset */
	I915_WRITE(DPIO_CTL, I915_READ(DPIO_CTL) & ~DPIO_CMNRST);

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
		    (I915_READ(DPLL(PIPE_B)) & DPLL_VCO_ENABLE) == 0)
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
	if (intel_wait_for_register(&dev_priv->uncore,
				    DISPLAY_PHY_STATUS,
				    phy_status_mask,
				    phy_status,
				    10))
		DRM_ERROR("Unexpected PHY_STATUS 0x%08x, expected 0x%08x (PHY_CONTROL=0x%08x)\n",
			  I915_READ(DISPLAY_PHY_STATUS) & phy_status_mask,
			   phy_status, dev_priv->chv_phy_control);
}

#undef BITS_SET

static void chv_dpio_cmn_power_well_enable(struct drm_i915_private *dev_priv,
					   struct i915_power_well *power_well)
{
	enum dpio_phy phy;
	enum pipe pipe;
	u32 tmp;

	WARN_ON_ONCE(power_well->desc->id != VLV_DISP_PW_DPIO_CMN_BC &&
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
	if (intel_wait_for_register(&dev_priv->uncore,
				    DISPLAY_PHY_STATUS,
				    PHY_POWERGOOD(phy),
				    PHY_POWERGOOD(phy),
				    1))
		DRM_ERROR("Display PHY %d is not power up\n", phy);

	mutex_lock(&dev_priv->sb_lock);

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

	mutex_unlock(&dev_priv->sb_lock);

	dev_priv->chv_phy_control |= PHY_COM_LANE_RESET_DEASSERT(phy);
	I915_WRITE(DISPLAY_PHY_CONTROL, dev_priv->chv_phy_control);

	DRM_DEBUG_KMS("Enabled DPIO PHY%d (PHY_CONTROL=0x%08x)\n",
		      phy, dev_priv->chv_phy_control);

	assert_chv_phy_status(dev_priv);
}

static void chv_dpio_cmn_power_well_disable(struct drm_i915_private *dev_priv,
					    struct i915_power_well *power_well)
{
	enum dpio_phy phy;

	WARN_ON_ONCE(power_well->desc->id != VLV_DISP_PW_DPIO_CMN_BC &&
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
	I915_WRITE(DISPLAY_PHY_CONTROL, dev_priv->chv_phy_control);

	vlv_set_power_well(dev_priv, power_well, false);

	DRM_DEBUG_KMS("Disabled DPIO PHY%d (PHY_CONTROL=0x%08x)\n",
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

	mutex_lock(&dev_priv->sb_lock);
	val = vlv_dpio_read(dev_priv, pipe, reg);
	mutex_unlock(&dev_priv->sb_lock);

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

	WARN(actual != expected,
	     "Unexpected DPIO lane power down: all %d, any %d. Expected: all %d, any %d. (0x%x = 0x%08x)\n",
	     !!(actual & DPIO_ALLDL_POWERDOWN), !!(actual & DPIO_ANYDL_POWERDOWN),
	     !!(expected & DPIO_ALLDL_POWERDOWN), !!(expected & DPIO_ANYDL_POWERDOWN),
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

	I915_WRITE(DISPLAY_PHY_CONTROL, dev_priv->chv_phy_control);

	DRM_DEBUG_KMS("Power gating DPIO PHY%d CH%d (DPIO_PHY_CONTROL=0x%08x)\n",
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
	enum dpio_phy phy = vlv_dport_to_phy(enc_to_dig_port(&encoder->base));
	enum dpio_channel ch = vlv_dport_to_channel(enc_to_dig_port(&encoder->base));

	mutex_lock(&power_domains->lock);

	dev_priv->chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD(0xf, phy, ch);
	dev_priv->chv_phy_control |= PHY_CH_POWER_DOWN_OVRD(mask, phy, ch);

	if (override)
		dev_priv->chv_phy_control |= PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);
	else
		dev_priv->chv_phy_control &= ~PHY_CH_POWER_DOWN_OVRD_EN(phy, ch);

	I915_WRITE(DISPLAY_PHY_CONTROL, dev_priv->chv_phy_control);

	DRM_DEBUG_KMS("Power gating DPIO PHY%d CH%d lanes 0x%x (PHY_CONTROL=0x%08x)\n",
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

	mutex_lock(&dev_priv->pcu_lock);

	state = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM) & DP_SSS_MASK(pipe);
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
	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM) & DP_SSC_MASK(pipe);
	WARN_ON(ctrl << 16 != state);

	mutex_unlock(&dev_priv->pcu_lock);

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

	mutex_lock(&dev_priv->pcu_lock);

#define COND \
	((vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM) & DP_SSS_MASK(pipe)) == state)

	if (COND)
		goto out;

	ctrl = vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM);
	ctrl &= ~DP_SSC_MASK(pipe);
	ctrl |= enable ? DP_SSC_PWR_ON(pipe) : DP_SSC_PWR_GATE(pipe);
	vlv_punit_write(dev_priv, PUNIT_REG_DSPSSPM, ctrl);

	if (wait_for(COND, 100))
		DRM_ERROR("timeout setting power well state %08x (%08x)\n",
			  state,
			  vlv_punit_read(dev_priv, PUNIT_REG_DSPSSPM));

#undef COND

out:
	mutex_unlock(&dev_priv->pcu_lock);
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

static void
__intel_display_power_get_domain(struct drm_i915_private *dev_priv,
				 enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *power_well;

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
	intel_wakeref_t wakeref = intel_runtime_pm_get(dev_priv);

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

	wakeref = intel_runtime_pm_get_if_in_use(dev_priv);
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
		intel_runtime_pm_put(dev_priv, wakeref);
		wakeref = 0;
	}

	return wakeref;
}

static void __intel_display_power_put(struct drm_i915_private *dev_priv,
				      enum intel_display_power_domain domain)
{
	struct i915_power_domains *power_domains;
	struct i915_power_well *power_well;

	power_domains = &dev_priv->power_domains;

	mutex_lock(&power_domains->lock);

	WARN(!power_domains->domain_use_count[domain],
	     "Use count on domain %s is already zero\n",
	     intel_display_power_domain_str(domain));
	power_domains->domain_use_count[domain]--;

	for_each_power_domain_well_reverse(dev_priv, power_well, BIT_ULL(domain))
		intel_power_well_put(dev_priv, power_well);

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
void intel_display_power_put_unchecked(struct drm_i915_private *dev_priv,
				       enum intel_display_power_domain domain)
{
	__intel_display_power_put(dev_priv, domain);
	intel_runtime_pm_put_unchecked(dev_priv);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain,
			     intel_wakeref_t wakeref)
{
	__intel_display_power_put(dev_priv, domain);
	intel_runtime_pm_put(dev_priv, wakeref);
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
	BIT_ULL(POWER_DOMAIN_PORT_DDI_B_IO) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_C_IO) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_D_IO) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_E_IO) |		\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_LANES) |	\
	BIT_ULL(POWER_DOMAIN_PORT_DDI_F_IO) |		\
	BIT_ULL(POWER_DOMAIN_AUX_B) |			\
	BIT_ULL(POWER_DOMAIN_AUX_C) |			\
	BIT_ULL(POWER_DOMAIN_AUX_D) |			\
	BIT_ULL(POWER_DOMAIN_AUX_E) |			\
	BIT_ULL(POWER_DOMAIN_AUX_F) |			\
	BIT_ULL(POWER_DOMAIN_AUX_TBT1) |		\
	BIT_ULL(POWER_DOMAIN_AUX_TBT2) |		\
	BIT_ULL(POWER_DOMAIN_AUX_TBT3) |		\
	BIT_ULL(POWER_DOMAIN_AUX_TBT4) |		\
	BIT_ULL(POWER_DOMAIN_VGA) |			\
	BIT_ULL(POWER_DOMAIN_AUDIO) |			\
	BIT_ULL(POWER_DOMAIN_INIT))
	/*
	 * - transcoder WD
	 * - KVMR (HW control)
	 */
#define ICL_PW_2_POWER_DOMAINS (			\
	ICL_PW_3_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_TRANSCODER_EDP_VDSC) |		\
	BIT_ULL(POWER_DOMAIN_INIT))
	/*
	 * - KVMR (HW control)
	 */
#define ICL_DISPLAY_DC_OFF_POWER_DOMAINS (		\
	ICL_PW_2_POWER_DOMAINS |			\
	BIT_ULL(POWER_DOMAIN_MODESET) |			\
	BIT_ULL(POWER_DOMAIN_AUX_A) |			\
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
#define ICL_AUX_C_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_C))
#define ICL_AUX_D_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_D))
#define ICL_AUX_E_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_E))
#define ICL_AUX_F_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_F))
#define ICL_AUX_TBT1_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_TBT1))
#define ICL_AUX_TBT2_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_TBT2))
#define ICL_AUX_TBT3_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_TBT3))
#define ICL_AUX_TBT4_IO_POWER_DOMAINS (			\
	BIT_ULL(POWER_DOMAIN_AUX_TBT4))

static const struct i915_power_well_ops i9xx_always_on_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
	.enable = i9xx_always_on_power_well_noop,
	.disable = i9xx_always_on_power_well_noop,
	.is_enabled = i9xx_always_on_power_well_enabled,
};

static const struct i915_power_well_ops chv_pipe_power_well_ops = {
	.sync_hw = i9xx_power_well_sync_hw_noop,
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
		.id = DISP_PW_ID_NONE,
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
		.id = DISP_PW_ID_NONE,
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
		.id = DISP_PW_ID_NONE,
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
		.id = DISP_PW_ID_NONE,
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
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = CNL_PW_CTL_IDX_DDI_F,
		},
	},
	{
		.name = "AUX F",
		.domains = CNL_DISPLAY_AUX_F_POWER_DOMAINS,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &hsw_power_well_regs,
			.hsw.idx = CNL_PW_CTL_IDX_AUX_F,
		},
	},
};

static const struct i915_power_well_ops icl_combo_phy_aux_power_well_ops = {
	.sync_hw = hsw_power_well_sync_hw,
	.enable = icl_combo_phy_aux_power_well_enable,
	.disable = icl_combo_phy_aux_power_well_disable,
	.is_enabled = hsw_power_well_enabled,
};

static const struct i915_power_well_ops icl_tc_phy_aux_power_well_ops = {
	.sync_hw = hsw_power_well_sync_hw,
	.enable = icl_tc_phy_aux_power_well_enable,
	.disable = hsw_power_well_disable,
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
		.id = DISP_PW_ID_NONE,
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
		.id = DISP_PW_ID_NONE,
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
		.ops = &icl_combo_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	},
	{
		.name = "AUX B",
		.domains = ICL_AUX_B_IO_POWER_DOMAINS,
		.ops = &icl_combo_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	},
	{
		.name = "AUX C",
		.domains = ICL_AUX_C_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_C,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX D",
		.domains = ICL_AUX_D_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_D,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX E",
		.domains = ICL_AUX_E_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_E,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX F",
		.domains = ICL_AUX_F_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_F,
			.hsw.is_tc_tbt = false,
		},
	},
	{
		.name = "AUX TBT1",
		.domains = ICL_AUX_TBT1_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT1,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX TBT2",
		.domains = ICL_AUX_TBT2_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT2,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX TBT3",
		.domains = ICL_AUX_TBT3_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.regs = &icl_aux_power_well_regs,
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT3,
			.hsw.is_tc_tbt = true,
		},
	},
	{
		.name = "AUX TBT4",
		.domains = ICL_AUX_TBT4_IO_POWER_DOMAINS,
		.ops = &icl_tc_phy_aux_power_well_ops,
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

	if (INTEL_GEN(dev_priv) >= 11) {
		max_dc = 2;
		/*
		 * DC9 has a separate HW flow from the rest of the DC states,
		 * not depending on the DMC firmware. It's needed by system
		 * suspend/resume, so allow it unconditionally.
		 */
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

	if (!i915_modparams.disable_power_well)
		max_dc = 0;

	if (enable_dc >= 0 && enable_dc <= max_dc) {
		requested_dc = enable_dc;
	} else if (enable_dc == -1) {
		requested_dc = max_dc;
	} else if (enable_dc > max_dc && enable_dc <= 2) {
		DRM_DEBUG_KMS("Adjusting requested max DC state (%d->%d)\n",
			      enable_dc, max_dc);
		requested_dc = max_dc;
	} else {
		DRM_ERROR("Unexpected value for enable_dc (%d)\n", enable_dc);
		requested_dc = max_dc;
	}

	if (requested_dc > 1)
		mask |= DC_STATE_EN_UPTO_DC6;
	if (requested_dc > 0)
		mask |= DC_STATE_EN_UPTO_DC5;

	DRM_DEBUG_KMS("Allowed DC state mask %02x\n", mask);

	return mask;
}

static int
__set_power_wells(struct i915_power_domains *power_domains,
		  const struct i915_power_well_desc *power_well_descs,
		  int power_well_count)
{
	u64 power_well_ids = 0;
	int i;

	power_domains->power_well_count = power_well_count;
	power_domains->power_wells =
				kcalloc(power_well_count,
					sizeof(*power_domains->power_wells),
					GFP_KERNEL);
	if (!power_domains->power_wells)
		return -ENOMEM;

	for (i = 0; i < power_well_count; i++) {
		enum i915_power_well_id id = power_well_descs[i].id;

		power_domains->power_wells[i].desc = &power_well_descs[i];

		if (id == DISP_PW_ID_NONE)
			continue;

		WARN_ON(id >= sizeof(power_well_ids) * 8);
		WARN_ON(power_well_ids & BIT_ULL(id));
		power_well_ids |= BIT_ULL(id);
	}

	return 0;
}

#define set_power_wells(power_domains, __power_well_descs) \
	__set_power_wells(power_domains, __power_well_descs, \
			  ARRAY_SIZE(__power_well_descs))

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

	i915_modparams.disable_power_well =
		sanitize_disable_power_well_option(dev_priv,
						   i915_modparams.disable_power_well);
	dev_priv->csr.allowed_dc_mask =
		get_allowed_dc_mask(dev_priv, i915_modparams.enable_dc);

	BUILD_BUG_ON(POWER_DOMAIN_NUM > 64);

	mutex_init(&power_domains->lock);

	/*
	 * The enabling order will be from lower to higher indexed wells,
	 * the disabling order is reversed.
	 */
	if (IS_GEN(dev_priv, 11)) {
		err = set_power_wells(power_domains, icl_power_wells);
	} else if (IS_CANNONLAKE(dev_priv)) {
		err = set_power_wells(power_domains, cnl_power_wells);

		/*
		 * DDI and Aux IO are getting enabled for all ports
		 * regardless the presence or use. So, in order to avoid
		 * timeouts, lets remove them from the list
		 * for the SKUs without port F.
		 */
		if (!IS_CNL_WITH_PORT_F(dev_priv))
			power_domains->power_well_count -= 2;
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

static inline
bool intel_dbuf_slice_set(struct drm_i915_private *dev_priv,
			  i915_reg_t reg, bool enable)
{
	u32 val, status;

	val = I915_READ(reg);
	val = enable ? (val | DBUF_POWER_REQUEST) : (val & ~DBUF_POWER_REQUEST);
	I915_WRITE(reg, val);
	POSTING_READ(reg);
	udelay(10);

	status = I915_READ(reg) & DBUF_POWER_STATE;
	if ((enable && !status) || (!enable && status)) {
		DRM_ERROR("DBus power %s timeout!\n",
			  enable ? "enable" : "disable");
		return false;
	}
	return true;
}

static void gen9_dbuf_enable(struct drm_i915_private *dev_priv)
{
	intel_dbuf_slice_set(dev_priv, DBUF_CTL, true);
}

static void gen9_dbuf_disable(struct drm_i915_private *dev_priv)
{
	intel_dbuf_slice_set(dev_priv, DBUF_CTL, false);
}

static u8 intel_dbuf_max_slices(struct drm_i915_private *dev_priv)
{
	if (INTEL_GEN(dev_priv) < 11)
		return 1;
	return 2;
}

void icl_dbuf_slices_update(struct drm_i915_private *dev_priv,
			    u8 req_slices)
{
	const u8 hw_enabled_slices = dev_priv->wm.skl_hw.ddb.enabled_slices;
	bool ret;

	if (req_slices > intel_dbuf_max_slices(dev_priv)) {
		DRM_ERROR("Invalid number of dbuf slices requested\n");
		return;
	}

	if (req_slices == hw_enabled_slices || req_slices == 0)
		return;

	if (req_slices > hw_enabled_slices)
		ret = intel_dbuf_slice_set(dev_priv, DBUF_CTL_S2, true);
	else
		ret = intel_dbuf_slice_set(dev_priv, DBUF_CTL_S2, false);

	if (ret)
		dev_priv->wm.skl_hw.ddb.enabled_slices = req_slices;
}

static void icl_dbuf_enable(struct drm_i915_private *dev_priv)
{
	I915_WRITE(DBUF_CTL_S1, I915_READ(DBUF_CTL_S1) | DBUF_POWER_REQUEST);
	I915_WRITE(DBUF_CTL_S2, I915_READ(DBUF_CTL_S2) | DBUF_POWER_REQUEST);
	POSTING_READ(DBUF_CTL_S2);

	udelay(10);

	if (!(I915_READ(DBUF_CTL_S1) & DBUF_POWER_STATE) ||
	    !(I915_READ(DBUF_CTL_S2) & DBUF_POWER_STATE))
		DRM_ERROR("DBuf power enable timeout\n");
	else
		/*
		 * FIXME: for now pretend that we only have 1 slice, see
		 * intel_enabled_dbuf_slices_num().
		 */
		dev_priv->wm.skl_hw.ddb.enabled_slices = 1;
}

static void icl_dbuf_disable(struct drm_i915_private *dev_priv)
{
	I915_WRITE(DBUF_CTL_S1, I915_READ(DBUF_CTL_S1) & ~DBUF_POWER_REQUEST);
	I915_WRITE(DBUF_CTL_S2, I915_READ(DBUF_CTL_S2) & ~DBUF_POWER_REQUEST);
	POSTING_READ(DBUF_CTL_S2);

	udelay(10);

	if ((I915_READ(DBUF_CTL_S1) & DBUF_POWER_STATE) ||
	    (I915_READ(DBUF_CTL_S2) & DBUF_POWER_STATE))
		DRM_ERROR("DBuf power disable timeout!\n");
	else
		/*
		 * FIXME: for now pretend that the first slice is always
		 * enabled, see intel_enabled_dbuf_slices_num().
		 */
		dev_priv->wm.skl_hw.ddb.enabled_slices = 1;
}

static void icl_mbus_init(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = MBUS_ABOX_BT_CREDIT_POOL1(16) |
	      MBUS_ABOX_BT_CREDIT_POOL2(16) |
	      MBUS_ABOX_B_CREDIT(1) |
	      MBUS_ABOX_BW_CREDIT(1);

	I915_WRITE(MBUS_ABOX_CTL, val);
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

	val = I915_READ(reg);

	if (enable)
		val |= reset_bits;
	else
		val &= ~reset_bits;

	I915_WRITE(reg, val);
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

	intel_cdclk_init(dev_priv);

	gen9_dbuf_enable(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);
}

static void skl_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	gen9_dbuf_disable(dev_priv);

	intel_cdclk_uninit(dev_priv);

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

void bxt_display_core_init(struct drm_i915_private *dev_priv,
			   bool resume)
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

	intel_cdclk_init(dev_priv);

	gen9_dbuf_enable(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);
}

void bxt_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	gen9_dbuf_disable(dev_priv);

	intel_cdclk_uninit(dev_priv);

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
	cnl_combo_phys_init(dev_priv);

	/*
	 * 4. Enable Power Well 1 (PG1).
	 *    The AUX IO power wells will be enabled on demand.
	 */
	mutex_lock(&power_domains->lock);
	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_enable(dev_priv, well);
	mutex_unlock(&power_domains->lock);

	/* 5. Enable CD clock */
	intel_cdclk_init(dev_priv);

	/* 6. Enable DBUF */
	gen9_dbuf_enable(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);
}

static void cnl_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	/* 1. Disable all display engine functions -> aready done */

	/* 2. Disable DBUF */
	gen9_dbuf_disable(dev_priv);

	/* 3. Disable CD clock */
	intel_cdclk_uninit(dev_priv);

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
	cnl_combo_phys_uninit(dev_priv);
}

void icl_display_core_init(struct drm_i915_private *dev_priv,
			   bool resume)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	/* 1. Enable PCH reset handshake. */
	intel_pch_reset_handshake(dev_priv, !HAS_PCH_NOP(dev_priv));

	/* 2-3. */
	icl_combo_phys_init(dev_priv);

	/*
	 * 4. Enable Power Well 1 (PG1).
	 *    The AUX IO power wells will be enabled on demand.
	 */
	mutex_lock(&power_domains->lock);
	well = lookup_power_well(dev_priv, SKL_DISP_PW_1);
	intel_power_well_enable(dev_priv, well);
	mutex_unlock(&power_domains->lock);

	/* 5. Enable CDCLK. */
	intel_cdclk_init(dev_priv);

	/* 6. Enable DBUF. */
	icl_dbuf_enable(dev_priv);

	/* 7. Setup MBUS. */
	icl_mbus_init(dev_priv);

	if (resume && dev_priv->csr.dmc_payload)
		intel_csr_load_program(dev_priv);
}

void icl_display_core_uninit(struct drm_i915_private *dev_priv)
{
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	struct i915_power_well *well;

	gen9_set_dc_state(dev_priv, DC_STATE_DISABLE);

	/* 1. Disable all display engine functions -> aready done */

	/* 2. Disable DBUF */
	icl_dbuf_disable(dev_priv);

	/* 3. Disable CD clock */
	intel_cdclk_uninit(dev_priv);

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
	icl_combo_phys_uninit(dev_priv);
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
		u32 status = I915_READ(DPLL(PIPE_A));
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
		u32 status = I915_READ(DPIO_PHY_STATUS);
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

	I915_WRITE(DISPLAY_PHY_CONTROL, dev_priv->chv_phy_control);

	DRM_DEBUG_KMS("Initial PHY_CONTROL=0x%08x\n",
		      dev_priv->chv_phy_control);
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
	    I915_READ(DPIO_CTL) & DPIO_CMNRST)
		return;

	DRM_DEBUG_KMS("toggling display PHY side reset\n");

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

	mutex_lock(&dev_priv->pcu_lock);
	ret = (vlv_punit_read(dev_priv, reg0) & SSPM0_SSC_MASK) == SSPM0_SSC_PWR_GATE;
	mutex_unlock(&dev_priv->pcu_lock);

	return ret;
}

static void assert_ved_power_gated(struct drm_i915_private *dev_priv)
{
	WARN(!vlv_punit_is_power_gated(dev_priv, PUNIT_REG_VEDSSPM0),
	     "VED not power gated\n");
}

static void assert_isp_power_gated(struct drm_i915_private *dev_priv)
{
	static const struct pci_device_id isp_ids[] = {
		{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0f38)},
		{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x22b8)},
		{}
	};

	WARN(!pci_dev_present(isp_ids) &&
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
 * intel_power_domains_fini_hw().
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
	} else if (IS_IVYBRIDGE(i915) || INTEL_GEN(i915) >= 7) {
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
	if (!i915_modparams.disable_power_well)
		intel_display_power_get(i915, POWER_DOMAIN_INIT);
	intel_power_domains_sync_hw(i915);

	power_domains->initializing = false;
}

/**
 * intel_power_domains_fini_hw - deinitialize hw power domain state
 * @i915: i915 device instance
 *
 * De-initializes the display power domain HW state. It also ensures that the
 * device stays powered up so that the driver can be reloaded.
 *
 * It must be called with power domains already disabled (after a call to
 * intel_power_domains_disable()) and must be paired with
 * intel_power_domains_init_hw().
 */
void intel_power_domains_fini_hw(struct drm_i915_private *i915)
{
	intel_wakeref_t wakeref __maybe_unused =
		fetch_and_zero(&i915->power_domains.wakeref);

	/* Remove the refcount we took to keep power well support disabled. */
	if (!i915_modparams.disable_power_well)
		intel_display_power_put_unchecked(i915, POWER_DOMAIN_INIT);

	intel_power_domains_verify_state(i915);

	/* Keep the power well enabled, but cancel its rpm wakeref. */
	intel_runtime_pm_put(i915, wakeref);
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

	WARN_ON(power_domains->wakeref);
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
		intel_power_domains_verify_state(i915);
		return;
	}

	/*
	 * Even if power well support was disabled we still want to disable
	 * power wells if power domains must be deinitialized for suspend.
	 */
	if (!i915_modparams.disable_power_well) {
		intel_display_power_put_unchecked(i915, POWER_DOMAIN_INIT);
		intel_power_domains_verify_state(i915);
	}

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
		WARN_ON(power_domains->wakeref);
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

		DRM_DEBUG_DRIVER("%-25s %d\n",
				 power_well->desc->name, power_well->count);

		for_each_power_domain(domain, power_well->desc->domains)
			DRM_DEBUG_DRIVER("  %-23s %d\n",
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

	dump_domain_info = false;
	for_each_power_well(i915, power_well) {
		enum intel_display_power_domain domain;
		int domains_count;
		bool enabled;

		enabled = power_well->desc->ops->is_enabled(i915, power_well);
		if ((power_well->count || power_well->desc->always_on) !=
		    enabled)
			DRM_ERROR("power well %s state mismatch (refcount %d/enabled %d)",
				  power_well->desc->name,
				  power_well->count, enabled);

		domains_count = 0;
		for_each_power_domain(domain, power_well->desc->domains)
			domains_count += power_domains->domain_use_count[domain];

		if (power_well->count != domains_count) {
			DRM_ERROR("power well %s refcount/domain refcount mismatch "
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

/**
 * intel_runtime_pm_get - grab a runtime pm reference
 * @i915: i915 device instance
 *
 * This function grabs a device-level runtime pm reference (mostly used for GEM
 * code to ensure the GTT or GT is on) and ensures that it is powered up.
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put() to release the reference again.
 *
 * Returns: the wakeref cookie to pass to intel_runtime_pm_put()
 */
intel_wakeref_t intel_runtime_pm_get(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;
	struct device *kdev = &pdev->dev;
	int ret;

	ret = pm_runtime_get_sync(kdev);
	WARN_ONCE(ret < 0, "pm_runtime_get_sync() failed: %d\n", ret);

	return track_intel_runtime_pm_wakeref(i915);
}

/**
 * intel_runtime_pm_get_if_in_use - grab a runtime pm reference if device in use
 * @i915: i915 device instance
 *
 * This function grabs a device-level runtime pm reference if the device is
 * already in use and ensures that it is powered up. It is illegal to try
 * and access the HW should intel_runtime_pm_get_if_in_use() report failure.
 *
 * Any runtime pm reference obtained by this function must have a symmetric
 * call to intel_runtime_pm_put() to release the reference again.
 *
 * Returns: the wakeref cookie to pass to intel_runtime_pm_put(), evaluates
 * as True if the wakeref was acquired, or False otherwise.
 */
intel_wakeref_t intel_runtime_pm_get_if_in_use(struct drm_i915_private *i915)
{
	if (IS_ENABLED(CONFIG_PM)) {
		struct pci_dev *pdev = i915->drm.pdev;
		struct device *kdev = &pdev->dev;

		/*
		 * In cases runtime PM is disabled by the RPM core and we get
		 * an -EINVAL return value we are not supposed to call this
		 * function, since the power state is undefined. This applies
		 * atm to the late/early system suspend/resume handlers.
		 */
		if (pm_runtime_get_if_in_use(kdev) <= 0)
			return 0;
	}

	return track_intel_runtime_pm_wakeref(i915);
}

/**
 * intel_runtime_pm_get_noresume - grab a runtime pm reference
 * @i915: i915 device instance
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
 *
 * Returns: the wakeref cookie to pass to intel_runtime_pm_put()
 */
intel_wakeref_t intel_runtime_pm_get_noresume(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;
	struct device *kdev = &pdev->dev;

	assert_rpm_wakelock_held(i915);
	pm_runtime_get_noresume(kdev);

	return track_intel_runtime_pm_wakeref(i915);
}

/**
 * intel_runtime_pm_put - release a runtime pm reference
 * @i915: i915 device instance
 *
 * This function drops the device-level runtime pm reference obtained by
 * intel_runtime_pm_get() and might power down the corresponding
 * hardware block right away if this is the last reference.
 */
void intel_runtime_pm_put_unchecked(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;
	struct device *kdev = &pdev->dev;

	untrack_intel_runtime_pm_wakeref(i915);

	pm_runtime_mark_last_busy(kdev);
	pm_runtime_put_autosuspend(kdev);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_runtime_pm_put(struct drm_i915_private *i915, intel_wakeref_t wref)
{
	cancel_intel_runtime_pm_wakeref(i915, wref);
	intel_runtime_pm_put_unchecked(i915);
}
#endif

/**
 * intel_runtime_pm_enable - enable runtime pm
 * @i915: i915 device instance
 *
 * This function enables runtime pm at the end of the driver load sequence.
 *
 * Note that this function does currently not enable runtime pm for the
 * subordinate display power domains. That is done by
 * intel_power_domains_enable().
 */
void intel_runtime_pm_enable(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;
	struct device *kdev = &pdev->dev;

	/*
	 * Disable the system suspend direct complete optimization, which can
	 * leave the device suspended skipping the driver's suspend handlers
	 * if the device was already runtime suspended. This is needed due to
	 * the difference in our runtime and system suspend sequence and
	 * becaue the HDA driver may require us to enable the audio power
	 * domain during system suspend.
	 */
	dev_pm_set_driver_flags(kdev, DPM_FLAG_NEVER_SKIP);

	pm_runtime_set_autosuspend_delay(kdev, 10000); /* 10s */
	pm_runtime_mark_last_busy(kdev);

	/*
	 * Take a permanent reference to disable the RPM functionality and drop
	 * it only when unloading the driver. Use the low level get/put helpers,
	 * so the driver's own RPM reference tracking asserts also work on
	 * platforms without RPM support.
	 */
	if (!HAS_RUNTIME_PM(i915)) {
		int ret;

		pm_runtime_dont_use_autosuspend(kdev);
		ret = pm_runtime_get_sync(kdev);
		WARN(ret < 0, "pm_runtime_get_sync() failed: %d\n", ret);
	} else {
		pm_runtime_use_autosuspend(kdev);
	}

	/*
	 * The core calls the driver load handler with an RPM reference held.
	 * We drop that here and will reacquire it during unloading in
	 * intel_power_domains_fini().
	 */
	pm_runtime_put_autosuspend(kdev);
}

void intel_runtime_pm_disable(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;
	struct device *kdev = &pdev->dev;

	/* Transfer rpm ownership back to core */
	WARN(pm_runtime_get_sync(kdev) < 0,
	     "Failed to pass rpm ownership back to core\n");

	pm_runtime_dont_use_autosuspend(kdev);

	if (!HAS_RUNTIME_PM(i915))
		pm_runtime_put(kdev);
}

void intel_runtime_pm_cleanup(struct drm_i915_private *i915)
{
	struct i915_runtime_pm *rpm = &i915->runtime_pm;
	int count;

	count = atomic_fetch_inc(&rpm->wakeref_count); /* balance untrack */
	WARN(count,
	     "i915->runtime_pm.wakeref_count=%d on cleanup\n",
	     count);

	untrack_intel_runtime_pm_wakeref(i915);
}

void intel_runtime_pm_init_early(struct drm_i915_private *i915)
{
	init_intel_runtime_pm_wakeref(i915);
}
