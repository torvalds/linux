// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap_hwmod implementation for OMAP2/3/4
 *
 * Copyright (C) 2009-2011 Nokia Corporation
 * Copyright (C) 2011-2012 Texas Instruments, Inc.
 *
 * Paul Walmsley, Benoît Cousson, Kevin Hilman
 *
 * Created in collaboration with (alphabetical order): Thara Gopinath,
 * Tony Lindgren, Rajendra Nayak, Vikram Pandita, Sakari Poussa, Anand
 * Sawant, Santosh Shilimkar, Richard Woodruff
 *
 * Introduction
 * ------------
 * One way to view an OMAP SoC is as a collection of largely unrelated
 * IP blocks connected by interconnects.  The IP blocks include
 * devices such as ARM processors, audio serial interfaces, UARTs,
 * etc.  Some of these devices, like the DSP, are created by TI;
 * others, like the SGX, largely originate from external vendors.  In
 * TI's documentation, on-chip devices are referred to as "OMAP
 * modules."  Some of these IP blocks are identical across several
 * OMAP versions.  Others are revised frequently.
 *
 * These OMAP modules are tied together by various interconnects.
 * Most of the address and data flow between modules is via OCP-based
 * interconnects such as the L3 and L4 buses; but there are other
 * interconnects that distribute the hardware clock tree, handle idle
 * and reset signaling, supply power, and connect the modules to
 * various pads or balls on the OMAP package.
 *
 * OMAP hwmod provides a consistent way to describe the on-chip
 * hardware blocks and their integration into the rest of the chip.
 * This description can be automatically generated from the TI
 * hardware database.  OMAP hwmod provides a standard, consistent API
 * to reset, enable, idle, and disable these hardware blocks.  And
 * hwmod provides a way for other core code, such as the Linux device
 * code or the OMAP power management and address space mapping code,
 * to query the hardware database.
 *
 * Using hwmod
 * -----------
 * Drivers won't call hwmod functions directly.  That is done by the
 * omap_device code, and in rare occasions, by custom integration code
 * in arch/arm/ *omap*.  The omap_device code includes functions to
 * build a struct platform_device using omap_hwmod data, and that is
 * currently how hwmod data is communicated to drivers and to the
 * Linux driver model.  Most drivers will call omap_hwmod functions only
 * indirectly, via pm_runtime*() functions.
 *
 * From a layering perspective, here is where the OMAP hwmod code
 * fits into the kernel software stack:
 *
 *            +-------------------------------+
 *            |      Device driver code       |
 *            |      (e.g., drivers/)         |
 *            +-------------------------------+
 *            |      Linux driver model       |
 *            |     (platform_device /        |
 *            |  platform_driver data/code)   |
 *            +-------------------------------+
 *            | OMAP core-driver integration  |
 *            |(arch/arm/mach-omap2/devices.c)|
 *            +-------------------------------+
 *            |      omap_device code         |
 *            | (../plat-omap/omap_device.c)  |
 *            +-------------------------------+
 *   ---->    |    omap_hwmod code/data       |    <-----
 *            | (../mach-omap2/omap_hwmod*)   |
 *            +-------------------------------+
 *            | OMAP clock/PRCM/register fns  |
 *            | ({read,write}l_relaxed, clk*) |
 *            +-------------------------------+
 *
 * Device drivers should not contain any OMAP-specific code or data in
 * them.  They should only contain code to operate the IP block that
 * the driver is responsible for.  This is because these IP blocks can
 * also appear in other SoCs, either from TI (such as DaVinci) or from
 * other manufacturers; and drivers should be reusable across other
 * platforms.
 *
 * The OMAP hwmod code also will attempt to reset and idle all on-chip
 * devices upon boot.  The goal here is for the kernel to be
 * completely self-reliant and independent from bootloaders.  This is
 * to ensure a repeatable configuration, both to ensure consistent
 * runtime behavior, and to make it easier for others to reproduce
 * bugs.
 *
 * OMAP module activity states
 * ---------------------------
 * The hwmod code considers modules to be in one of several activity
 * states.  IP blocks start out in an UNKNOWN state, then once they
 * are registered via the hwmod code, proceed to the REGISTERED state.
 * Once their clock names are resolved to clock pointers, the module
 * enters the CLKS_INITED state; and finally, once the module has been
 * reset and the integration registers programmed, the INITIALIZED state
 * is entered.  The hwmod code will then place the module into either
 * the IDLE state to save power, or in the case of a critical system
 * module, the ENABLED state.
 *
 * OMAP core integration code can then call omap_hwmod*() functions
 * directly to move the module between the IDLE, ENABLED, and DISABLED
 * states, as needed.  This is done during both the PM idle loop, and
 * in the OMAP core integration code's implementation of the PM runtime
 * functions.
 *
 * References
 * ----------
 * This is a partial list.
 * - OMAP2420 Multimedia Processor Silicon Revision 2.1.1, 2.2 (SWPU064)
 * - OMAP2430 Multimedia Device POP Silicon Revision 2.1 (SWPU090)
 * - OMAP34xx Multimedia Device Silicon Revision 3.1 (SWPU108)
 * - OMAP4430 Multimedia Device Silicon Revision 1.0 (SWPU140)
 * - Open Core Protocol Specification 2.2
 *
 * To do:
 * - handle IO mapping
 * - bus throughput & module latency measurement code
 *
 * XXX add tests at the beginning of each function to ensure the hwmod is
 * in the appropriate state
 * XXX error return values should be checked to ensure that they are
 * appropriate
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/memblock.h>

#include <linux/platform_data/ti-sysc.h>

#include <dt-bindings/bus/ti-sysc.h>

#include <asm/system_misc.h>

#include "clock.h"
#include "omap_hwmod.h"

#include "soc.h"
#include "common.h"
#include "clockdomain.h"
#include "hdq1w.h"
#include "mmc.h"
#include "powerdomain.h"
#include "cm2xxx.h"
#include "cm3xxx.h"
#include "cm33xx.h"
#include "prm.h"
#include "prm3xxx.h"
#include "prm44xx.h"
#include "prm33xx.h"
#include "prminst44xx.h"
#include "pm.h"
#include "wd_timer.h"

/* Name of the OMAP hwmod for the MPU */
#define MPU_INITIATOR_NAME		"mpu"

/*
 * Number of struct omap_hwmod_link records per struct
 * omap_hwmod_ocp_if record (master->slave and slave->master)
 */
#define LINKS_PER_OCP_IF		2

/*
 * Address offset (in bytes) between the reset control and the reset
 * status registers: 4 bytes on OMAP4
 */
#define OMAP4_RST_CTRL_ST_OFFSET	4

/*
 * Maximum length for module clock handle names
 */
#define MOD_CLK_MAX_NAME_LEN		32

/**
 * struct clkctrl_provider - clkctrl provider mapping data
 * @num_addrs: number of base address ranges for the provider
 * @addr: base address(es) for the provider
 * @size: size(s) of the provider address space(s)
 * @node: device node associated with the provider
 * @link: list link
 */
struct clkctrl_provider {
	int			num_addrs;
	u32			*addr;
	u32			*size;
	struct device_node	*node;
	struct list_head	link;
};

static LIST_HEAD(clkctrl_providers);

/**
 * struct omap_hwmod_reset - IP specific reset functions
 * @match: string to match against the module name
 * @len: number of characters to match
 * @reset: IP specific reset function
 *
 * Used only in cases where struct omap_hwmod is dynamically allocated.
 */
struct omap_hwmod_reset {
	const char *match;
	int len;
	int (*reset)(struct omap_hwmod *oh);
};

/**
 * struct omap_hwmod_soc_ops - fn ptrs for some SoC-specific operations
 * @enable_module: function to enable a module (via MODULEMODE)
 * @disable_module: function to disable a module (via MODULEMODE)
 *
 * XXX Eventually this functionality will be hidden inside the PRM/CM
 * device drivers.  Until then, this should avoid huge blocks of cpu_is_*()
 * conditionals in this code.
 */
struct omap_hwmod_soc_ops {
	void (*enable_module)(struct omap_hwmod *oh);
	int (*disable_module)(struct omap_hwmod *oh);
	int (*wait_target_ready)(struct omap_hwmod *oh);
	int (*assert_hardreset)(struct omap_hwmod *oh,
				struct omap_hwmod_rst_info *ohri);
	int (*deassert_hardreset)(struct omap_hwmod *oh,
				  struct omap_hwmod_rst_info *ohri);
	int (*is_hardreset_asserted)(struct omap_hwmod *oh,
				     struct omap_hwmod_rst_info *ohri);
	int (*init_clkdm)(struct omap_hwmod *oh);
	void (*update_context_lost)(struct omap_hwmod *oh);
	int (*get_context_lost)(struct omap_hwmod *oh);
	int (*disable_direct_prcm)(struct omap_hwmod *oh);
	u32 (*xlate_clkctrl)(struct omap_hwmod *oh);
};

/* soc_ops: adapts the omap_hwmod code to the currently-booted SoC */
static struct omap_hwmod_soc_ops soc_ops;

/* omap_hwmod_list contains all registered struct omap_hwmods */
static LIST_HEAD(omap_hwmod_list);
static DEFINE_MUTEX(list_lock);

/* mpu_oh: used to add/remove MPU initiator from sleepdep list */
static struct omap_hwmod *mpu_oh;

/* inited: set to true once the hwmod code is initialized */
static bool inited;

/* Private functions */

/**
 * _update_sysc_cache - return the module OCP_SYSCONFIG register, keep copy
 * @oh: struct omap_hwmod *
 *
 * Load the current value of the hwmod OCP_SYSCONFIG register into the
 * struct omap_hwmod for later use.  Returns -EINVAL if the hwmod has no
 * OCP_SYSCONFIG register or 0 upon success.
 */
static int _update_sysc_cache(struct omap_hwmod *oh)
{
	if (!oh->class->sysc) {
		WARN(1, "omap_hwmod: %s: cannot read OCP_SYSCONFIG: not defined on hwmod's class\n", oh->name);
		return -EINVAL;
	}

	/* XXX ensure module interface clock is up */

	oh->_sysc_cache = omap_hwmod_read(oh, oh->class->sysc->sysc_offs);

	if (!(oh->class->sysc->sysc_flags & SYSC_NO_CACHE))
		oh->_int_flags |= _HWMOD_SYSCONFIG_LOADED;

	return 0;
}

/**
 * _write_sysconfig - write a value to the module's OCP_SYSCONFIG register
 * @v: OCP_SYSCONFIG value to write
 * @oh: struct omap_hwmod *
 *
 * Write @v into the module class' OCP_SYSCONFIG register, if it has
 * one.  No return value.
 */
static void _write_sysconfig(u32 v, struct omap_hwmod *oh)
{
	if (!oh->class->sysc) {
		WARN(1, "omap_hwmod: %s: cannot write OCP_SYSCONFIG: not defined on hwmod's class\n", oh->name);
		return;
	}

	/* XXX ensure module interface clock is up */

	/* Module might have lost context, always update cache and register */
	oh->_sysc_cache = v;

	/*
	 * Some IP blocks (such as RTC) require unlocking of IP before
	 * accessing its registers. If a function pointer is present
	 * to unlock, then call it before accessing sysconfig and
	 * call lock after writing sysconfig.
	 */
	if (oh->class->unlock)
		oh->class->unlock(oh);

	omap_hwmod_write(v, oh, oh->class->sysc->sysc_offs);

	if (oh->class->lock)
		oh->class->lock(oh);
}

/**
 * _set_master_standbymode: set the OCP_SYSCONFIG MIDLEMODE field in @v
 * @oh: struct omap_hwmod *
 * @standbymode: MIDLEMODE field bits
 * @v: pointer to register contents to modify
 *
 * Update the master standby mode bits in @v to be @standbymode for
 * the @oh hwmod.  Does not write to the hardware.  Returns -EINVAL
 * upon error or 0 upon success.
 */
static int _set_master_standbymode(struct omap_hwmod *oh, u8 standbymode,
				   u32 *v)
{
	u32 mstandby_mask;
	u8 mstandby_shift;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_MIDLEMODE))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1, "omap_hwmod: %s: offset struct for sysconfig not provided in class\n", oh->name);
		return -EINVAL;
	}

	mstandby_shift = oh->class->sysc->sysc_fields->midle_shift;
	mstandby_mask = (0x3 << mstandby_shift);

	*v &= ~mstandby_mask;
	*v |= __ffs(standbymode) << mstandby_shift;

	return 0;
}

/**
 * _set_slave_idlemode: set the OCP_SYSCONFIG SIDLEMODE field in @v
 * @oh: struct omap_hwmod *
 * @idlemode: SIDLEMODE field bits
 * @v: pointer to register contents to modify
 *
 * Update the slave idle mode bits in @v to be @idlemode for the @oh
 * hwmod.  Does not write to the hardware.  Returns -EINVAL upon error
 * or 0 upon success.
 */
static int _set_slave_idlemode(struct omap_hwmod *oh, u8 idlemode, u32 *v)
{
	u32 sidle_mask;
	u8 sidle_shift;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_SIDLEMODE))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1, "omap_hwmod: %s: offset struct for sysconfig not provided in class\n", oh->name);
		return -EINVAL;
	}

	sidle_shift = oh->class->sysc->sysc_fields->sidle_shift;
	sidle_mask = (0x3 << sidle_shift);

	*v &= ~sidle_mask;
	*v |= __ffs(idlemode) << sidle_shift;

	return 0;
}

/**
 * _set_clockactivity: set OCP_SYSCONFIG.CLOCKACTIVITY bits in @v
 * @oh: struct omap_hwmod *
 * @clockact: CLOCKACTIVITY field bits
 * @v: pointer to register contents to modify
 *
 * Update the clockactivity mode bits in @v to be @clockact for the
 * @oh hwmod.  Used for additional powersaving on some modules.  Does
 * not write to the hardware.  Returns -EINVAL upon error or 0 upon
 * success.
 */
static int _set_clockactivity(struct omap_hwmod *oh, u8 clockact, u32 *v)
{
	u32 clkact_mask;
	u8  clkact_shift;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_CLOCKACTIVITY))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1, "omap_hwmod: %s: offset struct for sysconfig not provided in class\n", oh->name);
		return -EINVAL;
	}

	clkact_shift = oh->class->sysc->sysc_fields->clkact_shift;
	clkact_mask = (0x3 << clkact_shift);

	*v &= ~clkact_mask;
	*v |= clockact << clkact_shift;

	return 0;
}

/**
 * _set_softreset: set OCP_SYSCONFIG.SOFTRESET bit in @v
 * @oh: struct omap_hwmod *
 * @v: pointer to register contents to modify
 *
 * Set the SOFTRESET bit in @v for hwmod @oh.  Returns -EINVAL upon
 * error or 0 upon success.
 */
static int _set_softreset(struct omap_hwmod *oh, u32 *v)
{
	u32 softrst_mask;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_SOFTRESET))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1, "omap_hwmod: %s: offset struct for sysconfig not provided in class\n", oh->name);
		return -EINVAL;
	}

	softrst_mask = (0x1 << oh->class->sysc->sysc_fields->srst_shift);

	*v |= softrst_mask;

	return 0;
}

/**
 * _clear_softreset: clear OCP_SYSCONFIG.SOFTRESET bit in @v
 * @oh: struct omap_hwmod *
 * @v: pointer to register contents to modify
 *
 * Clear the SOFTRESET bit in @v for hwmod @oh.  Returns -EINVAL upon
 * error or 0 upon success.
 */
static int _clear_softreset(struct omap_hwmod *oh, u32 *v)
{
	u32 softrst_mask;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_SOFTRESET))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1,
		     "omap_hwmod: %s: sysc_fields absent for sysconfig class\n",
		     oh->name);
		return -EINVAL;
	}

	softrst_mask = (0x1 << oh->class->sysc->sysc_fields->srst_shift);

	*v &= ~softrst_mask;

	return 0;
}

/**
 * _wait_softreset_complete - wait for an OCP softreset to complete
 * @oh: struct omap_hwmod * to wait on
 *
 * Wait until the IP block represented by @oh reports that its OCP
 * softreset is complete.  This can be triggered by software (see
 * _ocp_softreset()) or by hardware upon returning from off-mode (one
 * example is HSMMC).  Waits for up to MAX_MODULE_SOFTRESET_WAIT
 * microseconds.  Returns the number of microseconds waited.
 */
static int _wait_softreset_complete(struct omap_hwmod *oh)
{
	struct omap_hwmod_class_sysconfig *sysc;
	u32 softrst_mask;
	int c = 0;

	sysc = oh->class->sysc;

	if (sysc->sysc_flags & SYSS_HAS_RESET_STATUS && sysc->syss_offs > 0)
		omap_test_timeout((omap_hwmod_read(oh, sysc->syss_offs)
				   & SYSS_RESETDONE_MASK),
				  MAX_MODULE_SOFTRESET_WAIT, c);
	else if (sysc->sysc_flags & SYSC_HAS_RESET_STATUS) {
		softrst_mask = (0x1 << sysc->sysc_fields->srst_shift);
		omap_test_timeout(!(omap_hwmod_read(oh, sysc->sysc_offs)
				    & softrst_mask),
				  MAX_MODULE_SOFTRESET_WAIT, c);
	}

	return c;
}

/**
 * _set_dmadisable: set OCP_SYSCONFIG.DMADISABLE bit in @v
 * @oh: struct omap_hwmod *
 *
 * The DMADISABLE bit is a semi-automatic bit present in sysconfig register
 * of some modules. When the DMA must perform read/write accesses, the
 * DMADISABLE bit is cleared by the hardware. But when the DMA must stop
 * for power management, software must set the DMADISABLE bit back to 1.
 *
 * Set the DMADISABLE bit in @v for hwmod @oh.  Returns -EINVAL upon
 * error or 0 upon success.
 */
static int _set_dmadisable(struct omap_hwmod *oh)
{
	u32 v;
	u32 dmadisable_mask;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_DMADISABLE))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1, "omap_hwmod: %s: offset struct for sysconfig not provided in class\n", oh->name);
		return -EINVAL;
	}

	/* clocks must be on for this operation */
	if (oh->_state != _HWMOD_STATE_ENABLED) {
		pr_warn("omap_hwmod: %s: dma can be disabled only from enabled state\n", oh->name);
		return -EINVAL;
	}

	pr_debug("omap_hwmod: %s: setting DMADISABLE\n", oh->name);

	v = oh->_sysc_cache;
	dmadisable_mask =
		(0x1 << oh->class->sysc->sysc_fields->dmadisable_shift);
	v |= dmadisable_mask;
	_write_sysconfig(v, oh);

	return 0;
}

/**
 * _set_module_autoidle: set the OCP_SYSCONFIG AUTOIDLE field in @v
 * @oh: struct omap_hwmod *
 * @autoidle: desired AUTOIDLE bitfield value (0 or 1)
 * @v: pointer to register contents to modify
 *
 * Update the module autoidle bit in @v to be @autoidle for the @oh
 * hwmod.  The autoidle bit controls whether the module can gate
 * internal clocks automatically when it isn't doing anything; the
 * exact function of this bit varies on a per-module basis.  This
 * function does not write to the hardware.  Returns -EINVAL upon
 * error or 0 upon success.
 */
static int _set_module_autoidle(struct omap_hwmod *oh, u8 autoidle,
				u32 *v)
{
	u32 autoidle_mask;
	u8 autoidle_shift;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_AUTOIDLE))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1, "omap_hwmod: %s: offset struct for sysconfig not provided in class\n", oh->name);
		return -EINVAL;
	}

	autoidle_shift = oh->class->sysc->sysc_fields->autoidle_shift;
	autoidle_mask = (0x1 << autoidle_shift);

	*v &= ~autoidle_mask;
	*v |= autoidle << autoidle_shift;

	return 0;
}

/**
 * _enable_wakeup: set OCP_SYSCONFIG.ENAWAKEUP bit in the hardware
 * @oh: struct omap_hwmod *
 *
 * Allow the hardware module @oh to send wakeups.  Returns -EINVAL
 * upon error or 0 upon success.
 */
static int _enable_wakeup(struct omap_hwmod *oh, u32 *v)
{
	if (!oh->class->sysc ||
	    !((oh->class->sysc->sysc_flags & SYSC_HAS_ENAWAKEUP) ||
	      (oh->class->sysc->idlemodes & SIDLE_SMART_WKUP) ||
	      (oh->class->sysc->idlemodes & MSTANDBY_SMART_WKUP)))
		return -EINVAL;

	if (!oh->class->sysc->sysc_fields) {
		WARN(1, "omap_hwmod: %s: offset struct for sysconfig not provided in class\n", oh->name);
		return -EINVAL;
	}

	if (oh->class->sysc->sysc_flags & SYSC_HAS_ENAWAKEUP)
		*v |= 0x1 << oh->class->sysc->sysc_fields->enwkup_shift;

	if (oh->class->sysc->idlemodes & SIDLE_SMART_WKUP)
		_set_slave_idlemode(oh, HWMOD_IDLEMODE_SMART_WKUP, v);
	if (oh->class->sysc->idlemodes & MSTANDBY_SMART_WKUP)
		_set_master_standbymode(oh, HWMOD_IDLEMODE_SMART_WKUP, v);

	/* XXX test pwrdm_get_wken for this hwmod's subsystem */

	return 0;
}

static struct clockdomain *_get_clkdm(struct omap_hwmod *oh)
{
	struct clk_hw_omap *clk;

	if (!oh)
		return NULL;

	if (oh->clkdm) {
		return oh->clkdm;
	} else if (oh->_clk) {
		if (!omap2_clk_is_hw_omap(__clk_get_hw(oh->_clk)))
			return NULL;
		clk = to_clk_hw_omap(__clk_get_hw(oh->_clk));
		return clk->clkdm;
	}
	return NULL;
}

/**
 * _add_initiator_dep: prevent @oh from smart-idling while @init_oh is active
 * @oh: struct omap_hwmod *
 *
 * Prevent the hardware module @oh from entering idle while the
 * hardare module initiator @init_oh is active.  Useful when a module
 * will be accessed by a particular initiator (e.g., if a module will
 * be accessed by the IVA, there should be a sleepdep between the IVA
 * initiator and the module).  Only applies to modules in smart-idle
 * mode.  If the clockdomain is marked as not needing autodeps, return
 * 0 without doing anything.  Otherwise, returns -EINVAL upon error or
 * passes along clkdm_add_sleepdep() value upon success.
 */
static int _add_initiator_dep(struct omap_hwmod *oh, struct omap_hwmod *init_oh)
{
	struct clockdomain *clkdm, *init_clkdm;

	clkdm = _get_clkdm(oh);
	init_clkdm = _get_clkdm(init_oh);

	if (!clkdm || !init_clkdm)
		return -EINVAL;

	if (clkdm && clkdm->flags & CLKDM_NO_AUTODEPS)
		return 0;

	return clkdm_add_sleepdep(clkdm, init_clkdm);
}

/**
 * _del_initiator_dep: allow @oh to smart-idle even if @init_oh is active
 * @oh: struct omap_hwmod *
 *
 * Allow the hardware module @oh to enter idle while the hardare
 * module initiator @init_oh is active.  Useful when a module will not
 * be accessed by a particular initiator (e.g., if a module will not
 * be accessed by the IVA, there should be no sleepdep between the IVA
 * initiator and the module).  Only applies to modules in smart-idle
 * mode.  If the clockdomain is marked as not needing autodeps, return
 * 0 without doing anything.  Returns -EINVAL upon error or passes
 * along clkdm_del_sleepdep() value upon success.
 */
static int _del_initiator_dep(struct omap_hwmod *oh, struct omap_hwmod *init_oh)
{
	struct clockdomain *clkdm, *init_clkdm;

	clkdm = _get_clkdm(oh);
	init_clkdm = _get_clkdm(init_oh);

	if (!clkdm || !init_clkdm)
		return -EINVAL;

	if (clkdm && clkdm->flags & CLKDM_NO_AUTODEPS)
		return 0;

	return clkdm_del_sleepdep(clkdm, init_clkdm);
}

static const struct of_device_id ti_clkctrl_match_table[] __initconst = {
	{ .compatible = "ti,clkctrl" },
	{ }
};

static int __init _setup_clkctrl_provider(struct device_node *np)
{
	const __be32 *addrp;
	struct clkctrl_provider *provider;
	u64 size;
	int i;

	provider = memblock_alloc(sizeof(*provider), SMP_CACHE_BYTES);
	if (!provider)
		return -ENOMEM;

	provider->node = np;

	provider->num_addrs =
		of_property_count_elems_of_size(np, "reg", sizeof(u32)) / 2;

	provider->addr =
		memblock_alloc(sizeof(void *) * provider->num_addrs,
			       SMP_CACHE_BYTES);
	if (!provider->addr)
		return -ENOMEM;

	provider->size =
		memblock_alloc(sizeof(u32) * provider->num_addrs,
			       SMP_CACHE_BYTES);
	if (!provider->size)
		return -ENOMEM;

	for (i = 0; i < provider->num_addrs; i++) {
		addrp = of_get_address(np, i, &size, NULL);
		provider->addr[i] = (u32)of_translate_address(np, addrp);
		provider->size[i] = size;
		pr_debug("%s: %pOF: %x...%x\n", __func__, np, provider->addr[i],
			 provider->addr[i] + provider->size[i]);
	}

	list_add(&provider->link, &clkctrl_providers);

	return 0;
}

static int __init _init_clkctrl_providers(void)
{
	struct device_node *np;
	int ret = 0;

	for_each_matching_node(np, ti_clkctrl_match_table) {
		ret = _setup_clkctrl_provider(np);
		if (ret)
			break;
	}

	return ret;
}

static u32 _omap4_xlate_clkctrl(struct omap_hwmod *oh)
{
	if (!oh->prcm.omap4.modulemode)
		return 0;

	return omap_cm_xlate_clkctrl(oh->clkdm->prcm_partition,
				     oh->clkdm->cm_inst,
				     oh->prcm.omap4.clkctrl_offs);
}

static struct clk *_lookup_clkctrl_clk(struct omap_hwmod *oh)
{
	struct clkctrl_provider *provider;
	struct clk *clk;
	u32 addr;

	if (!soc_ops.xlate_clkctrl)
		return NULL;

	addr = soc_ops.xlate_clkctrl(oh);
	if (!addr)
		return NULL;

	pr_debug("%s: %s: addr=%x\n", __func__, oh->name, addr);

	list_for_each_entry(provider, &clkctrl_providers, link) {
		int i;

		for (i = 0; i < provider->num_addrs; i++) {
			if (provider->addr[i] <= addr &&
			    provider->addr[i] + provider->size[i] > addr) {
				struct of_phandle_args clkspec;

				clkspec.np = provider->node;
				clkspec.args_count = 2;
				clkspec.args[0] = addr - provider->addr[0];
				clkspec.args[1] = 0;

				clk = of_clk_get_from_provider(&clkspec);

				pr_debug("%s: %s got %p (offset=%x, provider=%pOF)\n",
					 __func__, oh->name, clk,
					 clkspec.args[0], provider->node);

				return clk;
			}
		}
	}

	return NULL;
}

/**
 * _init_main_clk - get a struct clk * for the hwmod's main functional clk
 * @oh: struct omap_hwmod *
 *
 * Called from _init_clocks().  Populates the @oh _clk (main
 * functional clock pointer) if a clock matching the hwmod name is found,
 * or a main_clk is present.  Returns 0 on success or -EINVAL on error.
 */
static int _init_main_clk(struct omap_hwmod *oh)
{
	int ret = 0;
	struct clk *clk = NULL;

	clk = _lookup_clkctrl_clk(oh);

	if (!IS_ERR_OR_NULL(clk)) {
		pr_debug("%s: mapped main_clk %s for %s\n", __func__,
			 __clk_get_name(clk), oh->name);
		oh->main_clk = __clk_get_name(clk);
		oh->_clk = clk;
		soc_ops.disable_direct_prcm(oh);
	} else {
		if (!oh->main_clk)
			return 0;

		oh->_clk = clk_get(NULL, oh->main_clk);
	}

	if (IS_ERR(oh->_clk)) {
		pr_warn("omap_hwmod: %s: cannot clk_get main_clk %s\n",
			oh->name, oh->main_clk);
		return -EINVAL;
	}
	/*
	 * HACK: This needs a re-visit once clk_prepare() is implemented
	 * to do something meaningful. Today its just a no-op.
	 * If clk_prepare() is used at some point to do things like
	 * voltage scaling etc, then this would have to be moved to
	 * some point where subsystems like i2c and pmic become
	 * available.
	 */
	clk_prepare(oh->_clk);

	if (!_get_clkdm(oh))
		pr_debug("omap_hwmod: %s: missing clockdomain for %s.\n",
			   oh->name, oh->main_clk);

	return ret;
}

/**
 * _init_interface_clks - get a struct clk * for the hwmod's interface clks
 * @oh: struct omap_hwmod *
 *
 * Called from _init_clocks().  Populates the @oh OCP slave interface
 * clock pointers.  Returns 0 on success or -EINVAL on error.
 */
static int _init_interface_clks(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;
	struct clk *c;
	int ret = 0;

	list_for_each_entry(os, &oh->slave_ports, node) {
		if (!os->clk)
			continue;

		c = clk_get(NULL, os->clk);
		if (IS_ERR(c)) {
			pr_warn("omap_hwmod: %s: cannot clk_get interface_clk %s\n",
				oh->name, os->clk);
			ret = -EINVAL;
			continue;
		}
		os->_clk = c;
		/*
		 * HACK: This needs a re-visit once clk_prepare() is implemented
		 * to do something meaningful. Today its just a no-op.
		 * If clk_prepare() is used at some point to do things like
		 * voltage scaling etc, then this would have to be moved to
		 * some point where subsystems like i2c and pmic become
		 * available.
		 */
		clk_prepare(os->_clk);
	}

	return ret;
}

/**
 * _init_opt_clk - get a struct clk * for the hwmod's optional clocks
 * @oh: struct omap_hwmod *
 *
 * Called from _init_clocks().  Populates the @oh omap_hwmod_opt_clk
 * clock pointers.  Returns 0 on success or -EINVAL on error.
 */
static int _init_opt_clks(struct omap_hwmod *oh)
{
	struct omap_hwmod_opt_clk *oc;
	struct clk *c;
	int i;
	int ret = 0;

	for (i = oh->opt_clks_cnt, oc = oh->opt_clks; i > 0; i--, oc++) {
		c = clk_get(NULL, oc->clk);
		if (IS_ERR(c)) {
			pr_warn("omap_hwmod: %s: cannot clk_get opt_clk %s\n",
				oh->name, oc->clk);
			ret = -EINVAL;
			continue;
		}
		oc->_clk = c;
		/*
		 * HACK: This needs a re-visit once clk_prepare() is implemented
		 * to do something meaningful. Today its just a no-op.
		 * If clk_prepare() is used at some point to do things like
		 * voltage scaling etc, then this would have to be moved to
		 * some point where subsystems like i2c and pmic become
		 * available.
		 */
		clk_prepare(oc->_clk);
	}

	return ret;
}

static void _enable_optional_clocks(struct omap_hwmod *oh)
{
	struct omap_hwmod_opt_clk *oc;
	int i;

	pr_debug("omap_hwmod: %s: enabling optional clocks\n", oh->name);

	for (i = oh->opt_clks_cnt, oc = oh->opt_clks; i > 0; i--, oc++)
		if (oc->_clk) {
			pr_debug("omap_hwmod: enable %s:%s\n", oc->role,
				 __clk_get_name(oc->_clk));
			clk_enable(oc->_clk);
		}
}

static void _disable_optional_clocks(struct omap_hwmod *oh)
{
	struct omap_hwmod_opt_clk *oc;
	int i;

	pr_debug("omap_hwmod: %s: disabling optional clocks\n", oh->name);

	for (i = oh->opt_clks_cnt, oc = oh->opt_clks; i > 0; i--, oc++)
		if (oc->_clk) {
			pr_debug("omap_hwmod: disable %s:%s\n", oc->role,
				 __clk_get_name(oc->_clk));
			clk_disable(oc->_clk);
		}
}

/**
 * _enable_clocks - enable hwmod main clock and interface clocks
 * @oh: struct omap_hwmod *
 *
 * Enables all clocks necessary for register reads and writes to succeed
 * on the hwmod @oh.  Returns 0.
 */
static int _enable_clocks(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;

	pr_debug("omap_hwmod: %s: enabling clocks\n", oh->name);

	if (oh->flags & HWMOD_OPT_CLKS_NEEDED)
		_enable_optional_clocks(oh);

	if (oh->_clk)
		clk_enable(oh->_clk);

	list_for_each_entry(os, &oh->slave_ports, node) {
		if (os->_clk && (os->flags & OCPIF_SWSUP_IDLE)) {
			omap2_clk_deny_idle(os->_clk);
			clk_enable(os->_clk);
		}
	}

	/* The opt clocks are controlled by the device driver. */

	return 0;
}

/**
 * _omap4_clkctrl_managed_by_clkfwk - true if clkctrl managed by clock framework
 * @oh: struct omap_hwmod *
 */
static bool _omap4_clkctrl_managed_by_clkfwk(struct omap_hwmod *oh)
{
	if (oh->prcm.omap4.flags & HWMOD_OMAP4_CLKFWK_CLKCTR_CLOCK)
		return true;

	return false;
}

/**
 * _omap4_has_clkctrl_clock - returns true if a module has clkctrl clock
 * @oh: struct omap_hwmod *
 */
static bool _omap4_has_clkctrl_clock(struct omap_hwmod *oh)
{
	if (oh->prcm.omap4.clkctrl_offs)
		return true;

	if (!oh->prcm.omap4.clkctrl_offs &&
	    oh->prcm.omap4.flags & HWMOD_OMAP4_ZERO_CLKCTRL_OFFSET)
		return true;

	return false;
}

/**
 * _disable_clocks - disable hwmod main clock and interface clocks
 * @oh: struct omap_hwmod *
 *
 * Disables the hwmod @oh main functional and interface clocks.  Returns 0.
 */
static int _disable_clocks(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;

	pr_debug("omap_hwmod: %s: disabling clocks\n", oh->name);

	if (oh->_clk)
		clk_disable(oh->_clk);

	list_for_each_entry(os, &oh->slave_ports, node) {
		if (os->_clk && (os->flags & OCPIF_SWSUP_IDLE)) {
			clk_disable(os->_clk);
			omap2_clk_allow_idle(os->_clk);
		}
	}

	if (oh->flags & HWMOD_OPT_CLKS_NEEDED)
		_disable_optional_clocks(oh);

	/* The opt clocks are controlled by the device driver. */

	return 0;
}

/**
 * _omap4_enable_module - enable CLKCTRL modulemode on OMAP4
 * @oh: struct omap_hwmod *
 *
 * Enables the PRCM module mode related to the hwmod @oh.
 * No return value.
 */
static void _omap4_enable_module(struct omap_hwmod *oh)
{
	if (!oh->clkdm || !oh->prcm.omap4.modulemode ||
	    _omap4_clkctrl_managed_by_clkfwk(oh))
		return;

	pr_debug("omap_hwmod: %s: %s: %d\n",
		 oh->name, __func__, oh->prcm.omap4.modulemode);

	omap_cm_module_enable(oh->prcm.omap4.modulemode,
			      oh->clkdm->prcm_partition,
			      oh->clkdm->cm_inst, oh->prcm.omap4.clkctrl_offs);
}

/**
 * _omap4_wait_target_disable - wait for a module to be disabled on OMAP4
 * @oh: struct omap_hwmod *
 *
 * Wait for a module @oh to enter slave idle.  Returns 0 if the module
 * does not have an IDLEST bit or if the module successfully enters
 * slave idle; otherwise, pass along the return value of the
 * appropriate *_cm*_wait_module_idle() function.
 */
static int _omap4_wait_target_disable(struct omap_hwmod *oh)
{
	if (!oh)
		return -EINVAL;

	if (oh->_int_flags & _HWMOD_NO_MPU_PORT || !oh->clkdm)
		return 0;

	if (oh->flags & HWMOD_NO_IDLEST)
		return 0;

	if (_omap4_clkctrl_managed_by_clkfwk(oh))
		return 0;

	if (!_omap4_has_clkctrl_clock(oh))
		return 0;

	return omap_cm_wait_module_idle(oh->clkdm->prcm_partition,
					oh->clkdm->cm_inst,
					oh->prcm.omap4.clkctrl_offs, 0);
}

/**
 * _save_mpu_port_index - find and save the index to @oh's MPU port
 * @oh: struct omap_hwmod *
 *
 * Determines the array index of the OCP slave port that the MPU uses
 * to address the device, and saves it into the struct omap_hwmod.
 * Intended to be called during hwmod registration only. No return
 * value.
 */
static void __init _save_mpu_port_index(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os = NULL;

	if (!oh)
		return;

	oh->_int_flags |= _HWMOD_NO_MPU_PORT;

	list_for_each_entry(os, &oh->slave_ports, node) {
		if (os->user & OCP_USER_MPU) {
			oh->_mpu_port = os;
			oh->_int_flags &= ~_HWMOD_NO_MPU_PORT;
			break;
		}
	}

	return;
}

/**
 * _find_mpu_rt_port - return omap_hwmod_ocp_if accessible by the MPU
 * @oh: struct omap_hwmod *
 *
 * Given a pointer to a struct omap_hwmod record @oh, return a pointer
 * to the struct omap_hwmod_ocp_if record that is used by the MPU to
 * communicate with the IP block.  This interface need not be directly
 * connected to the MPU (and almost certainly is not), but is directly
 * connected to the IP block represented by @oh.  Returns a pointer
 * to the struct omap_hwmod_ocp_if * upon success, or returns NULL upon
 * error or if there does not appear to be a path from the MPU to this
 * IP block.
 */
static struct omap_hwmod_ocp_if *_find_mpu_rt_port(struct omap_hwmod *oh)
{
	if (!oh || oh->_int_flags & _HWMOD_NO_MPU_PORT || oh->slaves_cnt == 0)
		return NULL;

	return oh->_mpu_port;
};

/**
 * _enable_sysc - try to bring a module out of idle via OCP_SYSCONFIG
 * @oh: struct omap_hwmod *
 *
 * Ensure that the OCP_SYSCONFIG register for the IP block represented
 * by @oh is set to indicate to the PRCM that the IP block is active.
 * Usually this means placing the module into smart-idle mode and
 * smart-standby, but if there is a bug in the automatic idle handling
 * for the IP block, it may need to be placed into the force-idle or
 * no-idle variants of these modes.  No return value.
 */
static void _enable_sysc(struct omap_hwmod *oh)
{
	u8 idlemode, sf;
	u32 v;
	bool clkdm_act;
	struct clockdomain *clkdm;

	if (!oh->class->sysc)
		return;

	/*
	 * Wait until reset has completed, this is needed as the IP
	 * block is reset automatically by hardware in some cases
	 * (off-mode for example), and the drivers require the
	 * IP to be ready when they access it
	 */
	if (oh->flags & HWMOD_CONTROL_OPT_CLKS_IN_RESET)
		_enable_optional_clocks(oh);
	_wait_softreset_complete(oh);
	if (oh->flags & HWMOD_CONTROL_OPT_CLKS_IN_RESET)
		_disable_optional_clocks(oh);

	v = oh->_sysc_cache;
	sf = oh->class->sysc->sysc_flags;

	clkdm = _get_clkdm(oh);
	if (sf & SYSC_HAS_SIDLEMODE) {
		if (oh->flags & HWMOD_SWSUP_SIDLE ||
		    oh->flags & HWMOD_SWSUP_SIDLE_ACT) {
			idlemode = HWMOD_IDLEMODE_NO;
		} else {
			if (sf & SYSC_HAS_ENAWAKEUP)
				_enable_wakeup(oh, &v);
			if (oh->class->sysc->idlemodes & SIDLE_SMART_WKUP)
				idlemode = HWMOD_IDLEMODE_SMART_WKUP;
			else
				idlemode = HWMOD_IDLEMODE_SMART;
		}

		/*
		 * This is special handling for some IPs like
		 * 32k sync timer. Force them to idle!
		 */
		clkdm_act = (clkdm && clkdm->flags & CLKDM_ACTIVE_WITH_MPU);
		if (clkdm_act && !(oh->class->sysc->idlemodes &
				   (SIDLE_SMART | SIDLE_SMART_WKUP)))
			idlemode = HWMOD_IDLEMODE_FORCE;

		_set_slave_idlemode(oh, idlemode, &v);
	}

	if (sf & SYSC_HAS_MIDLEMODE) {
		if (oh->flags & HWMOD_FORCE_MSTANDBY) {
			idlemode = HWMOD_IDLEMODE_FORCE;
		} else if (oh->flags & HWMOD_SWSUP_MSTANDBY) {
			idlemode = HWMOD_IDLEMODE_NO;
		} else {
			if (sf & SYSC_HAS_ENAWAKEUP)
				_enable_wakeup(oh, &v);
			if (oh->class->sysc->idlemodes & MSTANDBY_SMART_WKUP)
				idlemode = HWMOD_IDLEMODE_SMART_WKUP;
			else
				idlemode = HWMOD_IDLEMODE_SMART;
		}
		_set_master_standbymode(oh, idlemode, &v);
	}

	/*
	 * XXX The clock framework should handle this, by
	 * calling into this code.  But this must wait until the
	 * clock structures are tagged with omap_hwmod entries
	 */
	if ((oh->flags & HWMOD_SET_DEFAULT_CLOCKACT) &&
	    (sf & SYSC_HAS_CLOCKACTIVITY))
		_set_clockactivity(oh, CLOCKACT_TEST_ICLK, &v);

	_write_sysconfig(v, oh);

	/*
	 * Set the autoidle bit only after setting the smartidle bit
	 * Setting this will not have any impact on the other modules.
	 */
	if (sf & SYSC_HAS_AUTOIDLE) {
		idlemode = (oh->flags & HWMOD_NO_OCP_AUTOIDLE) ?
			0 : 1;
		_set_module_autoidle(oh, idlemode, &v);
		_write_sysconfig(v, oh);
	}
}

/**
 * _idle_sysc - try to put a module into idle via OCP_SYSCONFIG
 * @oh: struct omap_hwmod *
 *
 * If module is marked as SWSUP_SIDLE, force the module into slave
 * idle; otherwise, configure it for smart-idle.  If module is marked
 * as SWSUP_MSUSPEND, force the module into master standby; otherwise,
 * configure it for smart-standby.  No return value.
 */
static void _idle_sysc(struct omap_hwmod *oh)
{
	u8 idlemode, sf;
	u32 v;

	if (!oh->class->sysc)
		return;

	v = oh->_sysc_cache;
	sf = oh->class->sysc->sysc_flags;

	if (sf & SYSC_HAS_SIDLEMODE) {
		if (oh->flags & HWMOD_SWSUP_SIDLE) {
			idlemode = HWMOD_IDLEMODE_FORCE;
		} else {
			if (sf & SYSC_HAS_ENAWAKEUP)
				_enable_wakeup(oh, &v);
			if (oh->class->sysc->idlemodes & SIDLE_SMART_WKUP)
				idlemode = HWMOD_IDLEMODE_SMART_WKUP;
			else
				idlemode = HWMOD_IDLEMODE_SMART;
		}
		_set_slave_idlemode(oh, idlemode, &v);
	}

	if (sf & SYSC_HAS_MIDLEMODE) {
		if ((oh->flags & HWMOD_SWSUP_MSTANDBY) ||
		    (oh->flags & HWMOD_FORCE_MSTANDBY)) {
			idlemode = HWMOD_IDLEMODE_FORCE;
		} else {
			if (sf & SYSC_HAS_ENAWAKEUP)
				_enable_wakeup(oh, &v);
			if (oh->class->sysc->idlemodes & MSTANDBY_SMART_WKUP)
				idlemode = HWMOD_IDLEMODE_SMART_WKUP;
			else
				idlemode = HWMOD_IDLEMODE_SMART;
		}
		_set_master_standbymode(oh, idlemode, &v);
	}

	/* If the cached value is the same as the new value, skip the write */
	if (oh->_sysc_cache != v)
		_write_sysconfig(v, oh);
}

/**
 * _shutdown_sysc - force a module into idle via OCP_SYSCONFIG
 * @oh: struct omap_hwmod *
 *
 * Force the module into slave idle and master suspend. No return
 * value.
 */
static void _shutdown_sysc(struct omap_hwmod *oh)
{
	u32 v;
	u8 sf;

	if (!oh->class->sysc)
		return;

	v = oh->_sysc_cache;
	sf = oh->class->sysc->sysc_flags;

	if (sf & SYSC_HAS_SIDLEMODE)
		_set_slave_idlemode(oh, HWMOD_IDLEMODE_FORCE, &v);

	if (sf & SYSC_HAS_MIDLEMODE)
		_set_master_standbymode(oh, HWMOD_IDLEMODE_FORCE, &v);

	if (sf & SYSC_HAS_AUTOIDLE)
		_set_module_autoidle(oh, 1, &v);

	_write_sysconfig(v, oh);
}

/**
 * _lookup - find an omap_hwmod by name
 * @name: find an omap_hwmod by name
 *
 * Return a pointer to an omap_hwmod by name, or NULL if not found.
 */
static struct omap_hwmod *_lookup(const char *name)
{
	struct omap_hwmod *oh, *temp_oh;

	oh = NULL;

	list_for_each_entry(temp_oh, &omap_hwmod_list, node) {
		if (!strcmp(name, temp_oh->name)) {
			oh = temp_oh;
			break;
		}
	}

	return oh;
}

/**
 * _init_clkdm - look up a clockdomain name, store pointer in omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Convert a clockdomain name stored in a struct omap_hwmod into a
 * clockdomain pointer, and save it into the struct omap_hwmod.
 * Return -EINVAL if the clkdm_name lookup failed.
 */
static int _init_clkdm(struct omap_hwmod *oh)
{
	if (!oh->clkdm_name) {
		pr_debug("omap_hwmod: %s: missing clockdomain\n", oh->name);
		return 0;
	}

	oh->clkdm = clkdm_lookup(oh->clkdm_name);
	if (!oh->clkdm) {
		pr_warn("omap_hwmod: %s: could not associate to clkdm %s\n",
			oh->name, oh->clkdm_name);
		return 0;
	}

	pr_debug("omap_hwmod: %s: associated to clkdm %s\n",
		oh->name, oh->clkdm_name);

	return 0;
}

/**
 * _init_clocks - clk_get() all clocks associated with this hwmod. Retrieve as
 * well the clockdomain.
 * @oh: struct omap_hwmod *
 * @np: device_node mapped to this hwmod
 *
 * Called by omap_hwmod_setup_*() (after omap2_clk_init()).
 * Resolves all clock names embedded in the hwmod.  Returns 0 on
 * success, or a negative error code on failure.
 */
static int _init_clocks(struct omap_hwmod *oh, struct device_node *np)
{
	int ret = 0;

	if (oh->_state != _HWMOD_STATE_REGISTERED)
		return 0;

	pr_debug("omap_hwmod: %s: looking up clocks\n", oh->name);

	if (soc_ops.init_clkdm)
		ret |= soc_ops.init_clkdm(oh);

	ret |= _init_main_clk(oh);
	ret |= _init_interface_clks(oh);
	ret |= _init_opt_clks(oh);

	if (!ret)
		oh->_state = _HWMOD_STATE_CLKS_INITED;
	else
		pr_warn("omap_hwmod: %s: cannot _init_clocks\n", oh->name);

	return ret;
}

/**
 * _lookup_hardreset - fill register bit info for this hwmod/reset line
 * @oh: struct omap_hwmod *
 * @name: name of the reset line in the context of this hwmod
 * @ohri: struct omap_hwmod_rst_info * that this function will fill in
 *
 * Return the bit position of the reset line that match the
 * input name. Return -ENOENT if not found.
 */
static int _lookup_hardreset(struct omap_hwmod *oh, const char *name,
			     struct omap_hwmod_rst_info *ohri)
{
	int i;

	for (i = 0; i < oh->rst_lines_cnt; i++) {
		const char *rst_line = oh->rst_lines[i].name;
		if (!strcmp(rst_line, name)) {
			ohri->rst_shift = oh->rst_lines[i].rst_shift;
			ohri->st_shift = oh->rst_lines[i].st_shift;
			pr_debug("omap_hwmod: %s: %s: %s: rst %d st %d\n",
				 oh->name, __func__, rst_line, ohri->rst_shift,
				 ohri->st_shift);

			return 0;
		}
	}

	return -ENOENT;
}

/**
 * _assert_hardreset - assert the HW reset line of submodules
 * contained in the hwmod module.
 * @oh: struct omap_hwmod *
 * @name: name of the reset line to lookup and assert
 *
 * Some IP like dsp, ipu or iva contain processor that require an HW
 * reset line to be assert / deassert in order to enable fully the IP.
 * Returns -EINVAL if @oh is null, -ENOSYS if we have no way of
 * asserting the hardreset line on the currently-booted SoC, or passes
 * along the return value from _lookup_hardreset() or the SoC's
 * assert_hardreset code.
 */
static int _assert_hardreset(struct omap_hwmod *oh, const char *name)
{
	struct omap_hwmod_rst_info ohri;
	int ret = -EINVAL;

	if (!oh)
		return -EINVAL;

	if (!soc_ops.assert_hardreset)
		return -ENOSYS;

	ret = _lookup_hardreset(oh, name, &ohri);
	if (ret < 0)
		return ret;

	ret = soc_ops.assert_hardreset(oh, &ohri);

	return ret;
}

/**
 * _deassert_hardreset - deassert the HW reset line of submodules contained
 * in the hwmod module.
 * @oh: struct omap_hwmod *
 * @name: name of the reset line to look up and deassert
 *
 * Some IP like dsp, ipu or iva contain processor that require an HW
 * reset line to be assert / deassert in order to enable fully the IP.
 * Returns -EINVAL if @oh is null, -ENOSYS if we have no way of
 * deasserting the hardreset line on the currently-booted SoC, or passes
 * along the return value from _lookup_hardreset() or the SoC's
 * deassert_hardreset code.
 */
static int _deassert_hardreset(struct omap_hwmod *oh, const char *name)
{
	struct omap_hwmod_rst_info ohri;
	int ret = -EINVAL;

	if (!oh)
		return -EINVAL;

	if (!soc_ops.deassert_hardreset)
		return -ENOSYS;

	ret = _lookup_hardreset(oh, name, &ohri);
	if (ret < 0)
		return ret;

	if (oh->clkdm) {
		/*
		 * A clockdomain must be in SW_SUP otherwise reset
		 * might not be completed. The clockdomain can be set
		 * in HW_AUTO only when the module become ready.
		 */
		clkdm_deny_idle(oh->clkdm);
		ret = clkdm_hwmod_enable(oh->clkdm, oh);
		if (ret) {
			WARN(1, "omap_hwmod: %s: could not enable clockdomain %s: %d\n",
			     oh->name, oh->clkdm->name, ret);
			return ret;
		}
	}

	_enable_clocks(oh);
	if (soc_ops.enable_module)
		soc_ops.enable_module(oh);

	ret = soc_ops.deassert_hardreset(oh, &ohri);

	if (soc_ops.disable_module)
		soc_ops.disable_module(oh);
	_disable_clocks(oh);

	if (ret == -EBUSY)
		pr_warn("omap_hwmod: %s: failed to hardreset\n", oh->name);

	if (oh->clkdm) {
		/*
		 * Set the clockdomain to HW_AUTO, assuming that the
		 * previous state was HW_AUTO.
		 */
		clkdm_allow_idle(oh->clkdm);

		clkdm_hwmod_disable(oh->clkdm, oh);
	}

	return ret;
}

/**
 * _read_hardreset - read the HW reset line state of submodules
 * contained in the hwmod module
 * @oh: struct omap_hwmod *
 * @name: name of the reset line to look up and read
 *
 * Return the state of the reset line.  Returns -EINVAL if @oh is
 * null, -ENOSYS if we have no way of reading the hardreset line
 * status on the currently-booted SoC, or passes along the return
 * value from _lookup_hardreset() or the SoC's is_hardreset_asserted
 * code.
 */
static int _read_hardreset(struct omap_hwmod *oh, const char *name)
{
	struct omap_hwmod_rst_info ohri;
	int ret = -EINVAL;

	if (!oh)
		return -EINVAL;

	if (!soc_ops.is_hardreset_asserted)
		return -ENOSYS;

	ret = _lookup_hardreset(oh, name, &ohri);
	if (ret < 0)
		return ret;

	return soc_ops.is_hardreset_asserted(oh, &ohri);
}

/**
 * _are_all_hardreset_lines_asserted - return true if the @oh is hard-reset
 * @oh: struct omap_hwmod *
 *
 * If all hardreset lines associated with @oh are asserted, then return true.
 * Otherwise, if part of @oh is out hardreset or if no hardreset lines
 * associated with @oh are asserted, then return false.
 * This function is used to avoid executing some parts of the IP block
 * enable/disable sequence if its hardreset line is set.
 */
static bool _are_all_hardreset_lines_asserted(struct omap_hwmod *oh)
{
	int i, rst_cnt = 0;

	if (oh->rst_lines_cnt == 0)
		return false;

	for (i = 0; i < oh->rst_lines_cnt; i++)
		if (_read_hardreset(oh, oh->rst_lines[i].name) > 0)
			rst_cnt++;

	if (oh->rst_lines_cnt == rst_cnt)
		return true;

	return false;
}

/**
 * _are_any_hardreset_lines_asserted - return true if any part of @oh is
 * hard-reset
 * @oh: struct omap_hwmod *
 *
 * If any hardreset lines associated with @oh are asserted, then
 * return true.  Otherwise, if no hardreset lines associated with @oh
 * are asserted, or if @oh has no hardreset lines, then return false.
 * This function is used to avoid executing some parts of the IP block
 * enable/disable sequence if any hardreset line is set.
 */
static bool _are_any_hardreset_lines_asserted(struct omap_hwmod *oh)
{
	int rst_cnt = 0;
	int i;

	for (i = 0; i < oh->rst_lines_cnt && rst_cnt == 0; i++)
		if (_read_hardreset(oh, oh->rst_lines[i].name) > 0)
			rst_cnt++;

	return (rst_cnt) ? true : false;
}

/**
 * _omap4_disable_module - enable CLKCTRL modulemode on OMAP4
 * @oh: struct omap_hwmod *
 *
 * Disable the PRCM module mode related to the hwmod @oh.
 * Return EINVAL if the modulemode is not supported and 0 in case of success.
 */
static int _omap4_disable_module(struct omap_hwmod *oh)
{
	int v;

	if (!oh->clkdm || !oh->prcm.omap4.modulemode ||
	    _omap4_clkctrl_managed_by_clkfwk(oh))
		return -EINVAL;

	/*
	 * Since integration code might still be doing something, only
	 * disable if all lines are under hardreset.
	 */
	if (_are_any_hardreset_lines_asserted(oh))
		return 0;

	pr_debug("omap_hwmod: %s: %s\n", oh->name, __func__);

	omap_cm_module_disable(oh->clkdm->prcm_partition, oh->clkdm->cm_inst,
			       oh->prcm.omap4.clkctrl_offs);

	v = _omap4_wait_target_disable(oh);
	if (v)
		pr_warn("omap_hwmod: %s: _wait_target_disable failed\n",
			oh->name);

	return 0;
}

/**
 * _ocp_softreset - reset an omap_hwmod via the OCP_SYSCONFIG bit
 * @oh: struct omap_hwmod *
 *
 * Resets an omap_hwmod @oh via the OCP_SYSCONFIG bit.  hwmod must be
 * enabled for this to work.  Returns -ENOENT if the hwmod cannot be
 * reset this way, -EINVAL if the hwmod is in the wrong state,
 * -ETIMEDOUT if the module did not reset in time, or 0 upon success.
 *
 * In OMAP3 a specific SYSSTATUS register is used to get the reset status.
 * Starting in OMAP4, some IPs do not have SYSSTATUS registers and instead
 * use the SYSCONFIG softreset bit to provide the status.
 *
 * Note that some IP like McBSP do have reset control but don't have
 * reset status.
 */
static int _ocp_softreset(struct omap_hwmod *oh)
{
	u32 v;
	int c = 0;
	int ret = 0;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_SOFTRESET))
		return -ENOENT;

	/* clocks must be on for this operation */
	if (oh->_state != _HWMOD_STATE_ENABLED) {
		pr_warn("omap_hwmod: %s: reset can only be entered from enabled state\n",
			oh->name);
		return -EINVAL;
	}

	/* For some modules, all optionnal clocks need to be enabled as well */
	if (oh->flags & HWMOD_CONTROL_OPT_CLKS_IN_RESET)
		_enable_optional_clocks(oh);

	pr_debug("omap_hwmod: %s: resetting via OCP SOFTRESET\n", oh->name);

	v = oh->_sysc_cache;
	ret = _set_softreset(oh, &v);
	if (ret)
		goto dis_opt_clks;

	_write_sysconfig(v, oh);

	if (oh->class->sysc->srst_udelay)
		udelay(oh->class->sysc->srst_udelay);

	c = _wait_softreset_complete(oh);
	if (c == MAX_MODULE_SOFTRESET_WAIT) {
		pr_warn("omap_hwmod: %s: softreset failed (waited %d usec)\n",
			oh->name, MAX_MODULE_SOFTRESET_WAIT);
		ret = -ETIMEDOUT;
		goto dis_opt_clks;
	} else {
		pr_debug("omap_hwmod: %s: softreset in %d usec\n", oh->name, c);
	}

	ret = _clear_softreset(oh, &v);
	if (ret)
		goto dis_opt_clks;

	_write_sysconfig(v, oh);

	/*
	 * XXX add _HWMOD_STATE_WEDGED for modules that don't come back from
	 * _wait_target_ready() or _reset()
	 */

dis_opt_clks:
	if (oh->flags & HWMOD_CONTROL_OPT_CLKS_IN_RESET)
		_disable_optional_clocks(oh);

	return ret;
}

/**
 * _reset - reset an omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Resets an omap_hwmod @oh.  If the module has a custom reset
 * function pointer defined, then call it to reset the IP block, and
 * pass along its return value to the caller.  Otherwise, if the IP
 * block has an OCP_SYSCONFIG register with a SOFTRESET bitfield
 * associated with it, call a function to reset the IP block via that
 * method, and pass along the return value to the caller.  Finally, if
 * the IP block has some hardreset lines associated with it, assert
 * all of those, but do _not_ deassert them. (This is because driver
 * authors have expressed an apparent requirement to control the
 * deassertion of the hardreset lines themselves.)
 *
 * The default software reset mechanism for most OMAP IP blocks is
 * triggered via the OCP_SYSCONFIG.SOFTRESET bit.  However, some
 * hwmods cannot be reset via this method.  Some are not targets and
 * therefore have no OCP header registers to access.  Others (like the
 * IVA) have idiosyncratic reset sequences.  So for these relatively
 * rare cases, custom reset code can be supplied in the struct
 * omap_hwmod_class .reset function pointer.
 *
 * _set_dmadisable() is called to set the DMADISABLE bit so that it
 * does not prevent idling of the system. This is necessary for cases
 * where ROMCODE/BOOTLOADER uses dma and transfers control to the
 * kernel without disabling dma.
 *
 * Passes along the return value from either _ocp_softreset() or the
 * custom reset function - these must return -EINVAL if the hwmod
 * cannot be reset this way or if the hwmod is in the wrong state,
 * -ETIMEDOUT if the module did not reset in time, or 0 upon success.
 */
static int _reset(struct omap_hwmod *oh)
{
	int i, r;

	pr_debug("omap_hwmod: %s: resetting\n", oh->name);

	if (oh->class->reset) {
		r = oh->class->reset(oh);
	} else {
		if (oh->rst_lines_cnt > 0) {
			for (i = 0; i < oh->rst_lines_cnt; i++)
				_assert_hardreset(oh, oh->rst_lines[i].name);
			return 0;
		} else {
			r = _ocp_softreset(oh);
			if (r == -ENOENT)
				r = 0;
		}
	}

	_set_dmadisable(oh);

	/*
	 * OCP_SYSCONFIG bits need to be reprogrammed after a
	 * softreset.  The _enable() function should be split to avoid
	 * the rewrite of the OCP_SYSCONFIG register.
	 */
	if (oh->class->sysc) {
		_update_sysc_cache(oh);
		_enable_sysc(oh);
	}

	return r;
}

/**
 * _omap4_update_context_lost - increment hwmod context loss counter if
 * hwmod context was lost, and clear hardware context loss reg
 * @oh: hwmod to check for context loss
 *
 * If the PRCM indicates that the hwmod @oh lost context, increment
 * our in-memory context loss counter, and clear the RM_*_CONTEXT
 * bits. No return value.
 */
static void _omap4_update_context_lost(struct omap_hwmod *oh)
{
	if (oh->prcm.omap4.flags & HWMOD_OMAP4_NO_CONTEXT_LOSS_BIT)
		return;

	if (!prm_was_any_context_lost_old(oh->clkdm->pwrdm.ptr->prcm_partition,
					  oh->clkdm->pwrdm.ptr->prcm_offs,
					  oh->prcm.omap4.context_offs))
		return;

	oh->prcm.omap4.context_lost_counter++;
	prm_clear_context_loss_flags_old(oh->clkdm->pwrdm.ptr->prcm_partition,
					 oh->clkdm->pwrdm.ptr->prcm_offs,
					 oh->prcm.omap4.context_offs);
}

/**
 * _omap4_get_context_lost - get context loss counter for a hwmod
 * @oh: hwmod to get context loss counter for
 *
 * Returns the in-memory context loss counter for a hwmod.
 */
static int _omap4_get_context_lost(struct omap_hwmod *oh)
{
	return oh->prcm.omap4.context_lost_counter;
}

/**
 * _enable - enable an omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Enables an omap_hwmod @oh such that the MPU can access the hwmod's
 * register target.  Returns -EINVAL if the hwmod is in the wrong
 * state or passes along the return value of _wait_target_ready().
 */
static int _enable(struct omap_hwmod *oh)
{
	int r;

	pr_debug("omap_hwmod: %s: enabling\n", oh->name);

	/*
	 * hwmods with HWMOD_INIT_NO_IDLE flag set are left in enabled
	 * state at init.
	 */
	if (oh->_int_flags & _HWMOD_SKIP_ENABLE) {
		oh->_int_flags &= ~_HWMOD_SKIP_ENABLE;
		return 0;
	}

	if (oh->_state != _HWMOD_STATE_INITIALIZED &&
	    oh->_state != _HWMOD_STATE_IDLE &&
	    oh->_state != _HWMOD_STATE_DISABLED) {
		WARN(1, "omap_hwmod: %s: enabled state can only be entered from initialized, idle, or disabled state\n",
			oh->name);
		return -EINVAL;
	}

	/*
	 * If an IP block contains HW reset lines and all of them are
	 * asserted, we let integration code associated with that
	 * block handle the enable.  We've received very little
	 * information on what those driver authors need, and until
	 * detailed information is provided and the driver code is
	 * posted to the public lists, this is probably the best we
	 * can do.
	 */
	if (_are_all_hardreset_lines_asserted(oh))
		return 0;

	_add_initiator_dep(oh, mpu_oh);

	if (oh->clkdm) {
		/*
		 * A clockdomain must be in SW_SUP before enabling
		 * completely the module. The clockdomain can be set
		 * in HW_AUTO only when the module become ready.
		 */
		clkdm_deny_idle(oh->clkdm);
		r = clkdm_hwmod_enable(oh->clkdm, oh);
		if (r) {
			WARN(1, "omap_hwmod: %s: could not enable clockdomain %s: %d\n",
			     oh->name, oh->clkdm->name, r);
			return r;
		}
	}

	_enable_clocks(oh);
	if (soc_ops.enable_module)
		soc_ops.enable_module(oh);
	if (oh->flags & HWMOD_BLOCK_WFI)
		cpu_idle_poll_ctrl(true);

	if (soc_ops.update_context_lost)
		soc_ops.update_context_lost(oh);

	r = (soc_ops.wait_target_ready) ? soc_ops.wait_target_ready(oh) :
		-EINVAL;
	if (oh->clkdm && !(oh->flags & HWMOD_CLKDM_NOAUTO))
		clkdm_allow_idle(oh->clkdm);

	if (!r) {
		oh->_state = _HWMOD_STATE_ENABLED;

		/* Access the sysconfig only if the target is ready */
		if (oh->class->sysc) {
			if (!(oh->_int_flags & _HWMOD_SYSCONFIG_LOADED))
				_update_sysc_cache(oh);
			_enable_sysc(oh);
		}
	} else {
		if (soc_ops.disable_module)
			soc_ops.disable_module(oh);
		_disable_clocks(oh);
		pr_err("omap_hwmod: %s: _wait_target_ready failed: %d\n",
		       oh->name, r);

		if (oh->clkdm)
			clkdm_hwmod_disable(oh->clkdm, oh);
	}

	return r;
}

/**
 * _idle - idle an omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Idles an omap_hwmod @oh.  This should be called once the hwmod has
 * no further work.  Returns -EINVAL if the hwmod is in the wrong
 * state or returns 0.
 */
static int _idle(struct omap_hwmod *oh)
{
	if (oh->flags & HWMOD_NO_IDLE) {
		oh->_int_flags |= _HWMOD_SKIP_ENABLE;
		return 0;
	}

	pr_debug("omap_hwmod: %s: idling\n", oh->name);

	if (_are_all_hardreset_lines_asserted(oh))
		return 0;

	if (oh->_state != _HWMOD_STATE_ENABLED) {
		WARN(1, "omap_hwmod: %s: idle state can only be entered from enabled state\n",
			oh->name);
		return -EINVAL;
	}

	if (oh->class->sysc)
		_idle_sysc(oh);
	_del_initiator_dep(oh, mpu_oh);

	/*
	 * If HWMOD_CLKDM_NOAUTO is set then we don't
	 * deny idle the clkdm again since idle was already denied
	 * in _enable()
	 */
	if (oh->clkdm && !(oh->flags & HWMOD_CLKDM_NOAUTO))
		clkdm_deny_idle(oh->clkdm);

	if (oh->flags & HWMOD_BLOCK_WFI)
		cpu_idle_poll_ctrl(false);
	if (soc_ops.disable_module)
		soc_ops.disable_module(oh);

	/*
	 * The module must be in idle mode before disabling any parents
	 * clocks. Otherwise, the parent clock might be disabled before
	 * the module transition is done, and thus will prevent the
	 * transition to complete properly.
	 */
	_disable_clocks(oh);
	if (oh->clkdm) {
		clkdm_allow_idle(oh->clkdm);
		clkdm_hwmod_disable(oh->clkdm, oh);
	}

	oh->_state = _HWMOD_STATE_IDLE;

	return 0;
}

/**
 * _shutdown - shutdown an omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Shut down an omap_hwmod @oh.  This should be called when the driver
 * used for the hwmod is removed or unloaded or if the driver is not
 * used by the system.  Returns -EINVAL if the hwmod is in the wrong
 * state or returns 0.
 */
static int _shutdown(struct omap_hwmod *oh)
{
	int ret, i;
	u8 prev_state;

	if (_are_all_hardreset_lines_asserted(oh))
		return 0;

	if (oh->_state != _HWMOD_STATE_IDLE &&
	    oh->_state != _HWMOD_STATE_ENABLED) {
		WARN(1, "omap_hwmod: %s: disabled state can only be entered from idle, or enabled state\n",
			oh->name);
		return -EINVAL;
	}

	pr_debug("omap_hwmod: %s: disabling\n", oh->name);

	if (oh->class->pre_shutdown) {
		prev_state = oh->_state;
		if (oh->_state == _HWMOD_STATE_IDLE)
			_enable(oh);
		ret = oh->class->pre_shutdown(oh);
		if (ret) {
			if (prev_state == _HWMOD_STATE_IDLE)
				_idle(oh);
			return ret;
		}
	}

	if (oh->class->sysc) {
		if (oh->_state == _HWMOD_STATE_IDLE)
			_enable(oh);
		_shutdown_sysc(oh);
	}

	/* clocks and deps are already disabled in idle */
	if (oh->_state == _HWMOD_STATE_ENABLED) {
		_del_initiator_dep(oh, mpu_oh);
		/* XXX what about the other system initiators here? dma, dsp */
		if (oh->flags & HWMOD_BLOCK_WFI)
			cpu_idle_poll_ctrl(false);
		if (soc_ops.disable_module)
			soc_ops.disable_module(oh);
		_disable_clocks(oh);
		if (oh->clkdm)
			clkdm_hwmod_disable(oh->clkdm, oh);
	}
	/* XXX Should this code also force-disable the optional clocks? */

	for (i = 0; i < oh->rst_lines_cnt; i++)
		_assert_hardreset(oh, oh->rst_lines[i].name);

	oh->_state = _HWMOD_STATE_DISABLED;

	return 0;
}

static int of_dev_find_hwmod(struct device_node *np,
			     struct omap_hwmod *oh)
{
	int count, i, res;
	const char *p;

	count = of_property_count_strings(np, "ti,hwmods");
	if (count < 1)
		return -ENODEV;

	for (i = 0; i < count; i++) {
		res = of_property_read_string_index(np, "ti,hwmods",
						    i, &p);
		if (res)
			continue;
		if (!strcmp(p, oh->name)) {
			pr_debug("omap_hwmod: dt %pOFn[%i] uses hwmod %s\n",
				 np, i, oh->name);
			return i;
		}
	}

	return -ENODEV;
}

/**
 * of_dev_hwmod_lookup - look up needed hwmod from dt blob
 * @np: struct device_node *
 * @oh: struct omap_hwmod *
 * @index: index of the entry found
 * @found: struct device_node * found or NULL
 *
 * Parse the dt blob and find out needed hwmod. Recursive function is
 * implemented to take care hierarchical dt blob parsing.
 * Return: Returns 0 on success, -ENODEV when not found.
 */
static int of_dev_hwmod_lookup(struct device_node *np,
			       struct omap_hwmod *oh,
			       int *index,
			       struct device_node **found)
{
	struct device_node *np0 = NULL;
	int res;

	res = of_dev_find_hwmod(np, oh);
	if (res >= 0) {
		*found = np;
		*index = res;
		return 0;
	}

	for_each_child_of_node(np, np0) {
		struct device_node *fc;
		int i;

		res = of_dev_hwmod_lookup(np0, oh, &i, &fc);
		if (res == 0) {
			*found = fc;
			*index = i;
			of_node_put(np0);
			return 0;
		}
	}

	*found = NULL;
	*index = 0;

	return -ENODEV;
}

/**
 * omap_hwmod_fix_mpu_rt_idx - fix up mpu_rt_idx register offsets
 *
 * @oh: struct omap_hwmod *
 * @np: struct device_node *
 *
 * Fix up module register offsets for modules with mpu_rt_idx.
 * Only needed for cpsw with interconnect target module defined
 * in device tree while still using legacy hwmod platform data
 * for rev, sysc and syss registers.
 *
 * Can be removed when all cpsw hwmod platform data has been
 * dropped.
 */
static void omap_hwmod_fix_mpu_rt_idx(struct omap_hwmod *oh,
				      struct device_node *np,
				      struct resource *res)
{
	struct device_node *child = NULL;
	int error;

	child = of_get_next_child(np, child);
	if (!child)
		return;

	error = of_address_to_resource(child, oh->mpu_rt_idx, res);
	if (error)
		pr_err("%s: error mapping mpu_rt_idx: %i\n",
		       __func__, error);
}

/**
 * omap_hwmod_parse_module_range - map module IO range from device tree
 * @oh: struct omap_hwmod *
 * @np: struct device_node *
 *
 * Parse the device tree range an interconnect target module provides
 * for it's child device IP blocks. This way we can support the old
 * "ti,hwmods" property with just dts data without a need for platform
 * data for IO resources. And we don't need all the child IP device
 * nodes available in the dts.
 */
int omap_hwmod_parse_module_range(struct omap_hwmod *oh,
				  struct device_node *np,
				  struct resource *res)
{
	struct property *prop;
	const __be32 *ranges;
	const char *name;
	u32 nr_addr, nr_size;
	u64 base, size;
	int len, error;

	if (!res)
		return -EINVAL;

	ranges = of_get_property(np, "ranges", &len);
	if (!ranges)
		return -ENOENT;

	len /= sizeof(*ranges);

	if (len < 3)
		return -EINVAL;

	of_property_for_each_string(np, "compatible", prop, name)
		if (!strncmp("ti,sysc-", name, 8))
			break;

	if (!name)
		return -ENOENT;

	error = of_property_read_u32(np, "#address-cells", &nr_addr);
	if (error)
		return -ENOENT;

	error = of_property_read_u32(np, "#size-cells", &nr_size);
	if (error)
		return -ENOENT;

	if (nr_addr != 1 || nr_size != 1) {
		pr_err("%s: invalid range for %s->%pOFn\n", __func__,
		       oh->name, np);
		return -EINVAL;
	}

	ranges++;
	base = of_translate_address(np, ranges++);
	size = be32_to_cpup(ranges);

	pr_debug("omap_hwmod: %s %pOFn at 0x%llx size 0x%llx\n",
		 oh->name, np, base, size);

	if (oh && oh->mpu_rt_idx) {
		omap_hwmod_fix_mpu_rt_idx(oh, np, res);

		return 0;
	}

	res->start = base;
	res->end = base + size - 1;
	res->flags = IORESOURCE_MEM;

	return 0;
}

/**
 * _init_mpu_rt_base - populate the virtual address for a hwmod
 * @oh: struct omap_hwmod * to locate the virtual address
 * @data: (unused, caller should pass NULL)
 * @index: index of the reg entry iospace in device tree
 * @np: struct device_node * of the IP block's device node in the DT data
 *
 * Cache the virtual address used by the MPU to access this IP block's
 * registers.  This address is needed early so the OCP registers that
 * are part of the device's address space can be ioremapped properly.
 *
 * If SYSC access is not needed, the registers will not be remapped
 * and non-availability of MPU access is not treated as an error.
 *
 * Returns 0 on success, -EINVAL if an invalid hwmod is passed, and
 * -ENXIO on absent or invalid register target address space.
 */
static int __init _init_mpu_rt_base(struct omap_hwmod *oh, void *data,
				    int index, struct device_node *np)
{
	void __iomem *va_start = NULL;
	struct resource res;
	int error;

	if (!oh)
		return -EINVAL;

	_save_mpu_port_index(oh);

	/* if we don't need sysc access we don't need to ioremap */
	if (!oh->class->sysc)
		return 0;

	/* we can't continue without MPU PORT if we need sysc access */
	if (oh->_int_flags & _HWMOD_NO_MPU_PORT)
		return -ENXIO;

	if (!np) {
		pr_err("omap_hwmod: %s: no dt node\n", oh->name);
		return -ENXIO;
	}

	/* Do we have a dts range for the interconnect target module? */
	error = omap_hwmod_parse_module_range(oh, np, &res);
	if (!error)
		va_start = ioremap(res.start, resource_size(&res));

	/* No ranges, rely on device reg entry */
	if (!va_start)
		va_start = of_iomap(np, index + oh->mpu_rt_idx);
	if (!va_start) {
		pr_err("omap_hwmod: %s: Missing dt reg%i for %pOF\n",
		       oh->name, index, np);
		return -ENXIO;
	}

	pr_debug("omap_hwmod: %s: MPU register target at va %p\n",
		 oh->name, va_start);

	oh->_mpu_rt_va = va_start;
	return 0;
}

static void __init parse_module_flags(struct omap_hwmod *oh,
				      struct device_node *np)
{
	if (of_find_property(np, "ti,no-reset-on-init", NULL))
		oh->flags |= HWMOD_INIT_NO_RESET;
	if (of_find_property(np, "ti,no-idle-on-init", NULL))
		oh->flags |= HWMOD_INIT_NO_IDLE;
	if (of_find_property(np, "ti,no-idle", NULL))
		oh->flags |= HWMOD_NO_IDLE;
}

/**
 * _init - initialize internal data for the hwmod @oh
 * @oh: struct omap_hwmod *
 * @n: (unused)
 *
 * Look up the clocks and the address space used by the MPU to access
 * registers belonging to the hwmod @oh.  @oh must already be
 * registered at this point.  This is the first of two phases for
 * hwmod initialization.  Code called here does not touch any hardware
 * registers, it simply prepares internal data structures.  Returns 0
 * upon success or if the hwmod isn't registered or if the hwmod's
 * address space is not defined, or -EINVAL upon failure.
 */
static int __init _init(struct omap_hwmod *oh, void *data)
{
	int r, index;
	struct device_node *np = NULL;
	struct device_node *bus;

	if (oh->_state != _HWMOD_STATE_REGISTERED)
		return 0;

	bus = of_find_node_by_name(NULL, "ocp");
	if (!bus)
		return -ENODEV;

	r = of_dev_hwmod_lookup(bus, oh, &index, &np);
	if (r)
		pr_debug("omap_hwmod: %s missing dt data\n", oh->name);
	else if (np && index)
		pr_warn("omap_hwmod: %s using broken dt data from %pOFn\n",
			oh->name, np);

	r = _init_mpu_rt_base(oh, NULL, index, np);
	if (r < 0) {
		WARN(1, "omap_hwmod: %s: doesn't have mpu register target base\n",
		     oh->name);
		return 0;
	}

	r = _init_clocks(oh, np);
	if (r < 0) {
		WARN(1, "omap_hwmod: %s: couldn't init clocks\n", oh->name);
		return -EINVAL;
	}

	if (np) {
		struct device_node *child;

		parse_module_flags(oh, np);
		child = of_get_next_child(np, NULL);
		if (child)
			parse_module_flags(oh, child);
	}

	oh->_state = _HWMOD_STATE_INITIALIZED;

	return 0;
}

/**
 * _setup_iclk_autoidle - configure an IP block's interface clocks
 * @oh: struct omap_hwmod *
 *
 * Set up the module's interface clocks.  XXX This function is still mostly
 * a stub; implementing this properly requires iclk autoidle usecounting in
 * the clock code.   No return value.
 */
static void _setup_iclk_autoidle(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;

	if (oh->_state != _HWMOD_STATE_INITIALIZED)
		return;

	list_for_each_entry(os, &oh->slave_ports, node) {
		if (!os->_clk)
			continue;

		if (os->flags & OCPIF_SWSUP_IDLE) {
			/*
			 * we might have multiple users of one iclk with
			 * different requirements, disable autoidle when
			 * the module is enabled, e.g. dss iclk
			 */
		} else {
			/* we are enabling autoidle afterwards anyways */
			clk_enable(os->_clk);
		}
	}

	return;
}

/**
 * _setup_reset - reset an IP block during the setup process
 * @oh: struct omap_hwmod *
 *
 * Reset the IP block corresponding to the hwmod @oh during the setup
 * process.  The IP block is first enabled so it can be successfully
 * reset.  Returns 0 upon success or a negative error code upon
 * failure.
 */
static int _setup_reset(struct omap_hwmod *oh)
{
	int r = 0;

	if (oh->_state != _HWMOD_STATE_INITIALIZED)
		return -EINVAL;

	if (oh->flags & HWMOD_EXT_OPT_MAIN_CLK)
		return -EPERM;

	if (oh->rst_lines_cnt == 0) {
		r = _enable(oh);
		if (r) {
			pr_warn("omap_hwmod: %s: cannot be enabled for reset (%d)\n",
				oh->name, oh->_state);
			return -EINVAL;
		}
	}

	if (!(oh->flags & HWMOD_INIT_NO_RESET))
		r = _reset(oh);

	return r;
}

/**
 * _setup_postsetup - transition to the appropriate state after _setup
 * @oh: struct omap_hwmod *
 *
 * Place an IP block represented by @oh into a "post-setup" state --
 * either IDLE, ENABLED, or DISABLED.  ("post-setup" simply means that
 * this function is called at the end of _setup().)  The postsetup
 * state for an IP block can be changed by calling
 * omap_hwmod_enter_postsetup_state() early in the boot process,
 * before one of the omap_hwmod_setup*() functions are called for the
 * IP block.
 *
 * The IP block stays in this state until a PM runtime-based driver is
 * loaded for that IP block.  A post-setup state of IDLE is
 * appropriate for almost all IP blocks with runtime PM-enabled
 * drivers, since those drivers are able to enable the IP block.  A
 * post-setup state of ENABLED is appropriate for kernels with PM
 * runtime disabled.  The DISABLED state is appropriate for unusual IP
 * blocks such as the MPU WDTIMER on kernels without WDTIMER drivers
 * included, since the WDTIMER starts running on reset and will reset
 * the MPU if left active.
 *
 * This post-setup mechanism is deprecated.  Once all of the OMAP
 * drivers have been converted to use PM runtime, and all of the IP
 * block data and interconnect data is available to the hwmod code, it
 * should be possible to replace this mechanism with a "lazy reset"
 * arrangement.  In a "lazy reset" setup, each IP block is enabled
 * when the driver first probes, then all remaining IP blocks without
 * drivers are either shut down or enabled after the drivers have
 * loaded.  However, this cannot take place until the above
 * preconditions have been met, since otherwise the late reset code
 * has no way of knowing which IP blocks are in use by drivers, and
 * which ones are unused.
 *
 * No return value.
 */
static void _setup_postsetup(struct omap_hwmod *oh)
{
	u8 postsetup_state;

	if (oh->rst_lines_cnt > 0)
		return;

	postsetup_state = oh->_postsetup_state;
	if (postsetup_state == _HWMOD_STATE_UNKNOWN)
		postsetup_state = _HWMOD_STATE_ENABLED;

	/*
	 * XXX HWMOD_INIT_NO_IDLE does not belong in hwmod data -
	 * it should be set by the core code as a runtime flag during startup
	 */
	if ((oh->flags & (HWMOD_INIT_NO_IDLE | HWMOD_NO_IDLE)) &&
	    (postsetup_state == _HWMOD_STATE_IDLE)) {
		oh->_int_flags |= _HWMOD_SKIP_ENABLE;
		postsetup_state = _HWMOD_STATE_ENABLED;
	}

	if (postsetup_state == _HWMOD_STATE_IDLE)
		_idle(oh);
	else if (postsetup_state == _HWMOD_STATE_DISABLED)
		_shutdown(oh);
	else if (postsetup_state != _HWMOD_STATE_ENABLED)
		WARN(1, "hwmod: %s: unknown postsetup state %d! defaulting to enabled\n",
		     oh->name, postsetup_state);

	return;
}

/**
 * _setup - prepare IP block hardware for use
 * @oh: struct omap_hwmod *
 * @n: (unused, pass NULL)
 *
 * Configure the IP block represented by @oh.  This may include
 * enabling the IP block, resetting it, and placing it into a
 * post-setup state, depending on the type of IP block and applicable
 * flags.  IP blocks are reset to prevent any previous configuration
 * by the bootloader or previous operating system from interfering
 * with power management or other parts of the system.  The reset can
 * be avoided; see omap_hwmod_no_setup_reset().  This is the second of
 * two phases for hwmod initialization.  Code called here generally
 * affects the IP block hardware, or system integration hardware
 * associated with the IP block.  Returns 0.
 */
static int _setup(struct omap_hwmod *oh, void *data)
{
	if (oh->_state != _HWMOD_STATE_INITIALIZED)
		return 0;

	if (oh->parent_hwmod) {
		int r;

		r = _enable(oh->parent_hwmod);
		WARN(r, "hwmod: %s: setup: failed to enable parent hwmod %s\n",
		     oh->name, oh->parent_hwmod->name);
	}

	_setup_iclk_autoidle(oh);

	if (!_setup_reset(oh))
		_setup_postsetup(oh);

	if (oh->parent_hwmod) {
		u8 postsetup_state;

		postsetup_state = oh->parent_hwmod->_postsetup_state;

		if (postsetup_state == _HWMOD_STATE_IDLE)
			_idle(oh->parent_hwmod);
		else if (postsetup_state == _HWMOD_STATE_DISABLED)
			_shutdown(oh->parent_hwmod);
		else if (postsetup_state != _HWMOD_STATE_ENABLED)
			WARN(1, "hwmod: %s: unknown postsetup state %d! defaulting to enabled\n",
			     oh->parent_hwmod->name, postsetup_state);
	}

	return 0;
}

/**
 * _register - register a struct omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Registers the omap_hwmod @oh.  Returns -EEXIST if an omap_hwmod
 * already has been registered by the same name; -EINVAL if the
 * omap_hwmod is in the wrong state, if @oh is NULL, if the
 * omap_hwmod's class field is NULL; if the omap_hwmod is missing a
 * name, or if the omap_hwmod's class is missing a name; or 0 upon
 * success.
 *
 * XXX The data should be copied into bootmem, so the original data
 * should be marked __initdata and freed after init.  This would allow
 * unneeded omap_hwmods to be freed on multi-OMAP configurations.  Note
 * that the copy process would be relatively complex due to the large number
 * of substructures.
 */
static int _register(struct omap_hwmod *oh)
{
	if (!oh || !oh->name || !oh->class || !oh->class->name ||
	    (oh->_state != _HWMOD_STATE_UNKNOWN))
		return -EINVAL;

	pr_debug("omap_hwmod: %s: registering\n", oh->name);

	if (_lookup(oh->name))
		return -EEXIST;

	list_add_tail(&oh->node, &omap_hwmod_list);

	INIT_LIST_HEAD(&oh->slave_ports);
	spin_lock_init(&oh->_lock);
	lockdep_set_class(&oh->_lock, &oh->hwmod_key);

	oh->_state = _HWMOD_STATE_REGISTERED;

	/*
	 * XXX Rather than doing a strcmp(), this should test a flag
	 * set in the hwmod data, inserted by the autogenerator code.
	 */
	if (!strcmp(oh->name, MPU_INITIATOR_NAME))
		mpu_oh = oh;

	return 0;
}

/**
 * _add_link - add an interconnect between two IP blocks
 * @oi: pointer to a struct omap_hwmod_ocp_if record
 *
 * Add struct omap_hwmod_link records connecting the slave IP block
 * specified in @oi->slave to @oi.  This code is assumed to run before
 * preemption or SMP has been enabled, thus avoiding the need for
 * locking in this code.  Changes to this assumption will require
 * additional locking.  Returns 0.
 */
static int _add_link(struct omap_hwmod_ocp_if *oi)
{
	pr_debug("omap_hwmod: %s -> %s: adding link\n", oi->master->name,
		 oi->slave->name);

	list_add(&oi->node, &oi->slave->slave_ports);
	oi->slave->slaves_cnt++;

	return 0;
}

/**
 * _register_link - register a struct omap_hwmod_ocp_if
 * @oi: struct omap_hwmod_ocp_if *
 *
 * Registers the omap_hwmod_ocp_if record @oi.  Returns -EEXIST if it
 * has already been registered; -EINVAL if @oi is NULL or if the
 * record pointed to by @oi is missing required fields; or 0 upon
 * success.
 *
 * XXX The data should be copied into bootmem, so the original data
 * should be marked __initdata and freed after init.  This would allow
 * unneeded omap_hwmods to be freed on multi-OMAP configurations.
 */
static int __init _register_link(struct omap_hwmod_ocp_if *oi)
{
	if (!oi || !oi->master || !oi->slave || !oi->user)
		return -EINVAL;

	if (oi->_int_flags & _OCPIF_INT_FLAGS_REGISTERED)
		return -EEXIST;

	pr_debug("omap_hwmod: registering link from %s to %s\n",
		 oi->master->name, oi->slave->name);

	/*
	 * Register the connected hwmods, if they haven't been
	 * registered already
	 */
	if (oi->master->_state != _HWMOD_STATE_REGISTERED)
		_register(oi->master);

	if (oi->slave->_state != _HWMOD_STATE_REGISTERED)
		_register(oi->slave);

	_add_link(oi);

	oi->_int_flags |= _OCPIF_INT_FLAGS_REGISTERED;

	return 0;
}

/* Static functions intended only for use in soc_ops field function pointers */

/**
 * _omap2xxx_3xxx_wait_target_ready - wait for a module to leave slave idle
 * @oh: struct omap_hwmod *
 *
 * Wait for a module @oh to leave slave idle.  Returns 0 if the module
 * does not have an IDLEST bit or if the module successfully leaves
 * slave idle; otherwise, pass along the return value of the
 * appropriate *_cm*_wait_module_ready() function.
 */
static int _omap2xxx_3xxx_wait_target_ready(struct omap_hwmod *oh)
{
	if (!oh)
		return -EINVAL;

	if (oh->flags & HWMOD_NO_IDLEST)
		return 0;

	if (!_find_mpu_rt_port(oh))
		return 0;

	/* XXX check module SIDLEMODE, hardreset status, enabled clocks */

	return omap_cm_wait_module_ready(0, oh->prcm.omap2.module_offs,
					 oh->prcm.omap2.idlest_reg_id,
					 oh->prcm.omap2.idlest_idle_bit);
}

/**
 * _omap4_wait_target_ready - wait for a module to leave slave idle
 * @oh: struct omap_hwmod *
 *
 * Wait for a module @oh to leave slave idle.  Returns 0 if the module
 * does not have an IDLEST bit or if the module successfully leaves
 * slave idle; otherwise, pass along the return value of the
 * appropriate *_cm*_wait_module_ready() function.
 */
static int _omap4_wait_target_ready(struct omap_hwmod *oh)
{
	if (!oh)
		return -EINVAL;

	if (oh->flags & HWMOD_NO_IDLEST || !oh->clkdm)
		return 0;

	if (!_find_mpu_rt_port(oh))
		return 0;

	if (_omap4_clkctrl_managed_by_clkfwk(oh))
		return 0;

	if (!_omap4_has_clkctrl_clock(oh))
		return 0;

	/* XXX check module SIDLEMODE, hardreset status */

	return omap_cm_wait_module_ready(oh->clkdm->prcm_partition,
					 oh->clkdm->cm_inst,
					 oh->prcm.omap4.clkctrl_offs, 0);
}

/**
 * _omap2_assert_hardreset - call OMAP2 PRM hardreset fn with hwmod args
 * @oh: struct omap_hwmod * to assert hardreset
 * @ohri: hardreset line data
 *
 * Call omap2_prm_assert_hardreset() with parameters extracted from
 * the hwmod @oh and the hardreset line data @ohri.  Only intended for
 * use as an soc_ops function pointer.  Passes along the return value
 * from omap2_prm_assert_hardreset().  XXX This function is scheduled
 * for removal when the PRM code is moved into drivers/.
 */
static int _omap2_assert_hardreset(struct omap_hwmod *oh,
				   struct omap_hwmod_rst_info *ohri)
{
	return omap_prm_assert_hardreset(ohri->rst_shift, 0,
					 oh->prcm.omap2.module_offs, 0);
}

/**
 * _omap2_deassert_hardreset - call OMAP2 PRM hardreset fn with hwmod args
 * @oh: struct omap_hwmod * to deassert hardreset
 * @ohri: hardreset line data
 *
 * Call omap2_prm_deassert_hardreset() with parameters extracted from
 * the hwmod @oh and the hardreset line data @ohri.  Only intended for
 * use as an soc_ops function pointer.  Passes along the return value
 * from omap2_prm_deassert_hardreset().  XXX This function is
 * scheduled for removal when the PRM code is moved into drivers/.
 */
static int _omap2_deassert_hardreset(struct omap_hwmod *oh,
				     struct omap_hwmod_rst_info *ohri)
{
	return omap_prm_deassert_hardreset(ohri->rst_shift, ohri->st_shift, 0,
					   oh->prcm.omap2.module_offs, 0, 0);
}

/**
 * _omap2_is_hardreset_asserted - call OMAP2 PRM hardreset fn with hwmod args
 * @oh: struct omap_hwmod * to test hardreset
 * @ohri: hardreset line data
 *
 * Call omap2_prm_is_hardreset_asserted() with parameters extracted
 * from the hwmod @oh and the hardreset line data @ohri.  Only
 * intended for use as an soc_ops function pointer.  Passes along the
 * return value from omap2_prm_is_hardreset_asserted().  XXX This
 * function is scheduled for removal when the PRM code is moved into
 * drivers/.
 */
static int _omap2_is_hardreset_asserted(struct omap_hwmod *oh,
					struct omap_hwmod_rst_info *ohri)
{
	return omap_prm_is_hardreset_asserted(ohri->st_shift, 0,
					      oh->prcm.omap2.module_offs, 0);
}

/**
 * _omap4_assert_hardreset - call OMAP4 PRM hardreset fn with hwmod args
 * @oh: struct omap_hwmod * to assert hardreset
 * @ohri: hardreset line data
 *
 * Call omap4_prminst_assert_hardreset() with parameters extracted
 * from the hwmod @oh and the hardreset line data @ohri.  Only
 * intended for use as an soc_ops function pointer.  Passes along the
 * return value from omap4_prminst_assert_hardreset().  XXX This
 * function is scheduled for removal when the PRM code is moved into
 * drivers/.
 */
static int _omap4_assert_hardreset(struct omap_hwmod *oh,
				   struct omap_hwmod_rst_info *ohri)
{
	if (!oh->clkdm)
		return -EINVAL;

	return omap_prm_assert_hardreset(ohri->rst_shift,
					 oh->clkdm->pwrdm.ptr->prcm_partition,
					 oh->clkdm->pwrdm.ptr->prcm_offs,
					 oh->prcm.omap4.rstctrl_offs);
}

/**
 * _omap4_deassert_hardreset - call OMAP4 PRM hardreset fn with hwmod args
 * @oh: struct omap_hwmod * to deassert hardreset
 * @ohri: hardreset line data
 *
 * Call omap4_prminst_deassert_hardreset() with parameters extracted
 * from the hwmod @oh and the hardreset line data @ohri.  Only
 * intended for use as an soc_ops function pointer.  Passes along the
 * return value from omap4_prminst_deassert_hardreset().  XXX This
 * function is scheduled for removal when the PRM code is moved into
 * drivers/.
 */
static int _omap4_deassert_hardreset(struct omap_hwmod *oh,
				     struct omap_hwmod_rst_info *ohri)
{
	if (!oh->clkdm)
		return -EINVAL;

	if (ohri->st_shift)
		pr_err("omap_hwmod: %s: %s: hwmod data error: OMAP4 does not support st_shift\n",
		       oh->name, ohri->name);
	return omap_prm_deassert_hardreset(ohri->rst_shift, ohri->rst_shift,
					   oh->clkdm->pwrdm.ptr->prcm_partition,
					   oh->clkdm->pwrdm.ptr->prcm_offs,
					   oh->prcm.omap4.rstctrl_offs,
					   oh->prcm.omap4.rstctrl_offs +
					   OMAP4_RST_CTRL_ST_OFFSET);
}

/**
 * _omap4_is_hardreset_asserted - call OMAP4 PRM hardreset fn with hwmod args
 * @oh: struct omap_hwmod * to test hardreset
 * @ohri: hardreset line data
 *
 * Call omap4_prminst_is_hardreset_asserted() with parameters
 * extracted from the hwmod @oh and the hardreset line data @ohri.
 * Only intended for use as an soc_ops function pointer.  Passes along
 * the return value from omap4_prminst_is_hardreset_asserted().  XXX
 * This function is scheduled for removal when the PRM code is moved
 * into drivers/.
 */
static int _omap4_is_hardreset_asserted(struct omap_hwmod *oh,
					struct omap_hwmod_rst_info *ohri)
{
	if (!oh->clkdm)
		return -EINVAL;

	return omap_prm_is_hardreset_asserted(ohri->rst_shift,
					      oh->clkdm->pwrdm.ptr->
					      prcm_partition,
					      oh->clkdm->pwrdm.ptr->prcm_offs,
					      oh->prcm.omap4.rstctrl_offs);
}

/**
 * _omap4_disable_direct_prcm - disable direct PRCM control for hwmod
 * @oh: struct omap_hwmod * to disable control for
 *
 * Disables direct PRCM clkctrl done by hwmod core. Instead, the hwmod
 * will be using its main_clk to enable/disable the module. Returns
 * 0 if successful.
 */
static int _omap4_disable_direct_prcm(struct omap_hwmod *oh)
{
	if (!oh)
		return -EINVAL;

	oh->prcm.omap4.flags |= HWMOD_OMAP4_CLKFWK_CLKCTR_CLOCK;

	return 0;
}

/**
 * _am33xx_deassert_hardreset - call AM33XX PRM hardreset fn with hwmod args
 * @oh: struct omap_hwmod * to deassert hardreset
 * @ohri: hardreset line data
 *
 * Call am33xx_prminst_deassert_hardreset() with parameters extracted
 * from the hwmod @oh and the hardreset line data @ohri.  Only
 * intended for use as an soc_ops function pointer.  Passes along the
 * return value from am33xx_prminst_deassert_hardreset().  XXX This
 * function is scheduled for removal when the PRM code is moved into
 * drivers/.
 */
static int _am33xx_deassert_hardreset(struct omap_hwmod *oh,
				     struct omap_hwmod_rst_info *ohri)
{
	return omap_prm_deassert_hardreset(ohri->rst_shift, ohri->st_shift,
					   oh->clkdm->pwrdm.ptr->prcm_partition,
					   oh->clkdm->pwrdm.ptr->prcm_offs,
					   oh->prcm.omap4.rstctrl_offs,
					   oh->prcm.omap4.rstst_offs);
}

/* Public functions */

u32 omap_hwmod_read(struct omap_hwmod *oh, u16 reg_offs)
{
	if (oh->flags & HWMOD_16BIT_REG)
		return readw_relaxed(oh->_mpu_rt_va + reg_offs);
	else
		return readl_relaxed(oh->_mpu_rt_va + reg_offs);
}

void omap_hwmod_write(u32 v, struct omap_hwmod *oh, u16 reg_offs)
{
	if (oh->flags & HWMOD_16BIT_REG)
		writew_relaxed(v, oh->_mpu_rt_va + reg_offs);
	else
		writel_relaxed(v, oh->_mpu_rt_va + reg_offs);
}

/**
 * omap_hwmod_softreset - reset a module via SYSCONFIG.SOFTRESET bit
 * @oh: struct omap_hwmod *
 *
 * This is a public function exposed to drivers. Some drivers may need to do
 * some settings before and after resetting the device.  Those drivers after
 * doing the necessary settings could use this function to start a reset by
 * setting the SYSCONFIG.SOFTRESET bit.
 */
int omap_hwmod_softreset(struct omap_hwmod *oh)
{
	u32 v;
	int ret;

	if (!oh || !(oh->_sysc_cache))
		return -EINVAL;

	v = oh->_sysc_cache;
	ret = _set_softreset(oh, &v);
	if (ret)
		goto error;
	_write_sysconfig(v, oh);

	ret = _clear_softreset(oh, &v);
	if (ret)
		goto error;
	_write_sysconfig(v, oh);

error:
	return ret;
}

/**
 * omap_hwmod_lookup - look up a registered omap_hwmod by name
 * @name: name of the omap_hwmod to look up
 *
 * Given a @name of an omap_hwmod, return a pointer to the registered
 * struct omap_hwmod *, or NULL upon error.
 */
struct omap_hwmod *omap_hwmod_lookup(const char *name)
{
	struct omap_hwmod *oh;

	if (!name)
		return NULL;

	oh = _lookup(name);

	return oh;
}

/**
 * omap_hwmod_for_each - call function for each registered omap_hwmod
 * @fn: pointer to a callback function
 * @data: void * data to pass to callback function
 *
 * Call @fn for each registered omap_hwmod, passing @data to each
 * function.  @fn must return 0 for success or any other value for
 * failure.  If @fn returns non-zero, the iteration across omap_hwmods
 * will stop and the non-zero return value will be passed to the
 * caller of omap_hwmod_for_each().  @fn is called with
 * omap_hwmod_for_each() held.
 */
int omap_hwmod_for_each(int (*fn)(struct omap_hwmod *oh, void *data),
			void *data)
{
	struct omap_hwmod *temp_oh;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	list_for_each_entry(temp_oh, &omap_hwmod_list, node) {
		ret = (*fn)(temp_oh, data);
		if (ret)
			break;
	}

	return ret;
}

/**
 * omap_hwmod_register_links - register an array of hwmod links
 * @ois: pointer to an array of omap_hwmod_ocp_if to register
 *
 * Intended to be called early in boot before the clock framework is
 * initialized.  If @ois is not null, will register all omap_hwmods
 * listed in @ois that are valid for this chip.  Returns -EINVAL if
 * omap_hwmod_init() hasn't been called before calling this function,
 * -ENOMEM if the link memory area can't be allocated, or 0 upon
 * success.
 */
int __init omap_hwmod_register_links(struct omap_hwmod_ocp_if **ois)
{
	int r, i;

	if (!inited)
		return -EINVAL;

	if (!ois)
		return 0;

	if (ois[0] == NULL) /* Empty list */
		return 0;

	i = 0;
	do {
		r = _register_link(ois[i]);
		WARN(r && r != -EEXIST,
		     "omap_hwmod: _register_link(%s -> %s) returned %d\n",
		     ois[i]->master->name, ois[i]->slave->name, r);
	} while (ois[++i]);

	return 0;
}

/**
 * _ensure_mpu_hwmod_is_setup - ensure the MPU SS hwmod is init'ed and set up
 * @oh: pointer to the hwmod currently being set up (usually not the MPU)
 *
 * If the hwmod data corresponding to the MPU subsystem IP block
 * hasn't been initialized and set up yet, do so now.  This must be
 * done first since sleep dependencies may be added from other hwmods
 * to the MPU.  Intended to be called only by omap_hwmod_setup*().  No
 * return value.
 */
static void __init _ensure_mpu_hwmod_is_setup(struct omap_hwmod *oh)
{
	if (!mpu_oh || mpu_oh->_state == _HWMOD_STATE_UNKNOWN)
		pr_err("omap_hwmod: %s: MPU initiator hwmod %s not yet registered\n",
		       __func__, MPU_INITIATOR_NAME);
	else if (mpu_oh->_state == _HWMOD_STATE_REGISTERED && oh != mpu_oh)
		omap_hwmod_setup_one(MPU_INITIATOR_NAME);
}

/**
 * omap_hwmod_setup_one - set up a single hwmod
 * @oh_name: const char * name of the already-registered hwmod to set up
 *
 * Initialize and set up a single hwmod.  Intended to be used for a
 * small number of early devices, such as the timer IP blocks used for
 * the scheduler clock.  Must be called after omap2_clk_init().
 * Resolves the struct clk names to struct clk pointers for each
 * registered omap_hwmod.  Also calls _setup() on each hwmod.  Returns
 * -EINVAL upon error or 0 upon success.
 */
int __init omap_hwmod_setup_one(const char *oh_name)
{
	struct omap_hwmod *oh;

	pr_debug("omap_hwmod: %s: %s\n", oh_name, __func__);

	oh = _lookup(oh_name);
	if (!oh) {
		WARN(1, "omap_hwmod: %s: hwmod not yet registered\n", oh_name);
		return -EINVAL;
	}

	_ensure_mpu_hwmod_is_setup(oh);

	_init(oh, NULL);
	_setup(oh, NULL);

	return 0;
}

static void omap_hwmod_check_one(struct device *dev,
				 const char *name, s8 v1, u8 v2)
{
	if (v1 < 0)
		return;

	if (v1 != v2)
		dev_warn(dev, "%s %d != %d\n", name, v1, v2);
}

/**
 * omap_hwmod_check_sysc - check sysc against platform sysc
 * @dev: struct device
 * @data: module data
 * @sysc_fields: new sysc configuration
 */
static int omap_hwmod_check_sysc(struct device *dev,
				 const struct ti_sysc_module_data *data,
				 struct sysc_regbits *sysc_fields)
{
	const struct sysc_regbits *regbits = data->cap->regbits;

	omap_hwmod_check_one(dev, "dmadisable_shift",
			     regbits->dmadisable_shift,
			     sysc_fields->dmadisable_shift);
	omap_hwmod_check_one(dev, "midle_shift",
			     regbits->midle_shift,
			     sysc_fields->midle_shift);
	omap_hwmod_check_one(dev, "sidle_shift",
			     regbits->sidle_shift,
			     sysc_fields->sidle_shift);
	omap_hwmod_check_one(dev, "clkact_shift",
			     regbits->clkact_shift,
			     sysc_fields->clkact_shift);
	omap_hwmod_check_one(dev, "enwkup_shift",
			     regbits->enwkup_shift,
			     sysc_fields->enwkup_shift);
	omap_hwmod_check_one(dev, "srst_shift",
			     regbits->srst_shift,
			     sysc_fields->srst_shift);
	omap_hwmod_check_one(dev, "autoidle_shift",
			     regbits->autoidle_shift,
			     sysc_fields->autoidle_shift);

	return 0;
}

/**
 * omap_hwmod_init_regbits - init sysconfig specific register bits
 * @dev: struct device
 * @oh: module
 * @data: module data
 * @sysc_fields: new sysc configuration
 */
static int omap_hwmod_init_regbits(struct device *dev, struct omap_hwmod *oh,
				   const struct ti_sysc_module_data *data,
				   struct sysc_regbits **sysc_fields)
{
	switch (data->cap->type) {
	case TI_SYSC_OMAP2:
	case TI_SYSC_OMAP2_TIMER:
		*sysc_fields = &omap_hwmod_sysc_type1;
		break;
	case TI_SYSC_OMAP3_SHAM:
		*sysc_fields = &omap3_sham_sysc_fields;
		break;
	case TI_SYSC_OMAP3_AES:
		*sysc_fields = &omap3xxx_aes_sysc_fields;
		break;
	case TI_SYSC_OMAP4:
	case TI_SYSC_OMAP4_TIMER:
		*sysc_fields = &omap_hwmod_sysc_type2;
		break;
	case TI_SYSC_OMAP4_SIMPLE:
		*sysc_fields = &omap_hwmod_sysc_type3;
		break;
	case TI_SYSC_OMAP34XX_SR:
		*sysc_fields = &omap34xx_sr_sysc_fields;
		break;
	case TI_SYSC_OMAP36XX_SR:
		*sysc_fields = &omap36xx_sr_sysc_fields;
		break;
	case TI_SYSC_OMAP4_SR:
		*sysc_fields = &omap36xx_sr_sysc_fields;
		break;
	case TI_SYSC_OMAP4_MCASP:
		*sysc_fields = &omap_hwmod_sysc_type_mcasp;
		break;
	case TI_SYSC_OMAP4_USB_HOST_FS:
		*sysc_fields = &omap_hwmod_sysc_type_usb_host_fs;
		break;
	default:
		*sysc_fields = NULL;
		if (!oh->class->sysc->sysc_fields)
			return 0;

		dev_err(dev, "sysc_fields not found\n");

		return -EINVAL;
	}

	return omap_hwmod_check_sysc(dev, data, *sysc_fields);
}

/**
 * omap_hwmod_init_reg_offs - initialize sysconfig register offsets
 * @dev: struct device
 * @data: module data
 * @rev_offs: revision register offset
 * @sysc_offs: sysc register offset
 * @syss_offs: syss register offset
 */
static int omap_hwmod_init_reg_offs(struct device *dev,
				    const struct ti_sysc_module_data *data,
				    s32 *rev_offs, s32 *sysc_offs,
				    s32 *syss_offs)
{
	*rev_offs = -ENODEV;
	*sysc_offs = 0;
	*syss_offs = 0;

	if (data->offsets[SYSC_REVISION] >= 0)
		*rev_offs = data->offsets[SYSC_REVISION];

	if (data->offsets[SYSC_SYSCONFIG] >= 0)
		*sysc_offs = data->offsets[SYSC_SYSCONFIG];

	if (data->offsets[SYSC_SYSSTATUS] >= 0)
		*syss_offs = data->offsets[SYSC_SYSSTATUS];

	return 0;
}

/**
 * omap_hwmod_init_sysc_flags - initialize sysconfig features
 * @dev: struct device
 * @data: module data
 * @sysc_flags: module configuration
 */
static int omap_hwmod_init_sysc_flags(struct device *dev,
				      const struct ti_sysc_module_data *data,
				      u32 *sysc_flags)
{
	*sysc_flags = 0;

	switch (data->cap->type) {
	case TI_SYSC_OMAP2:
	case TI_SYSC_OMAP2_TIMER:
		/* See SYSC_OMAP2_* in include/dt-bindings/bus/ti-sysc.h */
		if (data->cfg->sysc_val & SYSC_OMAP2_CLOCKACTIVITY)
			*sysc_flags |= SYSC_HAS_CLOCKACTIVITY;
		if (data->cfg->sysc_val & SYSC_OMAP2_EMUFREE)
			*sysc_flags |= SYSC_HAS_EMUFREE;
		if (data->cfg->sysc_val & SYSC_OMAP2_ENAWAKEUP)
			*sysc_flags |= SYSC_HAS_ENAWAKEUP;
		if (data->cfg->sysc_val & SYSC_OMAP2_SOFTRESET)
			*sysc_flags |= SYSC_HAS_SOFTRESET;
		if (data->cfg->sysc_val & SYSC_OMAP2_AUTOIDLE)
			*sysc_flags |= SYSC_HAS_AUTOIDLE;
		break;
	case TI_SYSC_OMAP4:
	case TI_SYSC_OMAP4_TIMER:
		/* See SYSC_OMAP4_* in include/dt-bindings/bus/ti-sysc.h */
		if (data->cfg->sysc_val & SYSC_OMAP4_DMADISABLE)
			*sysc_flags |= SYSC_HAS_DMADISABLE;
		if (data->cfg->sysc_val & SYSC_OMAP4_FREEEMU)
			*sysc_flags |= SYSC_HAS_EMUFREE;
		if (data->cfg->sysc_val & SYSC_OMAP4_SOFTRESET)
			*sysc_flags |= SYSC_HAS_SOFTRESET;
		break;
	case TI_SYSC_OMAP34XX_SR:
	case TI_SYSC_OMAP36XX_SR:
		/* See SYSC_OMAP3_SR_* in include/dt-bindings/bus/ti-sysc.h */
		if (data->cfg->sysc_val & SYSC_OMAP3_SR_ENAWAKEUP)
			*sysc_flags |= SYSC_HAS_ENAWAKEUP;
		break;
	default:
		if (data->cap->regbits->emufree_shift >= 0)
			*sysc_flags |= SYSC_HAS_EMUFREE;
		if (data->cap->regbits->enwkup_shift >= 0)
			*sysc_flags |= SYSC_HAS_ENAWAKEUP;
		if (data->cap->regbits->srst_shift >= 0)
			*sysc_flags |= SYSC_HAS_SOFTRESET;
		if (data->cap->regbits->autoidle_shift >= 0)
			*sysc_flags |= SYSC_HAS_AUTOIDLE;
		break;
	}

	if (data->cap->regbits->midle_shift >= 0 &&
	    data->cfg->midlemodes)
		*sysc_flags |= SYSC_HAS_MIDLEMODE;

	if (data->cap->regbits->sidle_shift >= 0 &&
	    data->cfg->sidlemodes)
		*sysc_flags |= SYSC_HAS_SIDLEMODE;

	if (data->cfg->quirks & SYSC_QUIRK_UNCACHED)
		*sysc_flags |= SYSC_NO_CACHE;
	if (data->cfg->quirks & SYSC_QUIRK_RESET_STATUS)
		*sysc_flags |= SYSC_HAS_RESET_STATUS;

	if (data->cfg->syss_mask & 1)
		*sysc_flags |= SYSS_HAS_RESET_STATUS;

	return 0;
}

/**
 * omap_hwmod_init_idlemodes - initialize module idle modes
 * @dev: struct device
 * @data: module data
 * @idlemodes: module supported idle modes
 */
static int omap_hwmod_init_idlemodes(struct device *dev,
				     const struct ti_sysc_module_data *data,
				     u32 *idlemodes)
{
	*idlemodes = 0;

	if (data->cfg->midlemodes & BIT(SYSC_IDLE_FORCE))
		*idlemodes |= MSTANDBY_FORCE;
	if (data->cfg->midlemodes & BIT(SYSC_IDLE_NO))
		*idlemodes |= MSTANDBY_NO;
	if (data->cfg->midlemodes & BIT(SYSC_IDLE_SMART))
		*idlemodes |= MSTANDBY_SMART;
	if (data->cfg->midlemodes & BIT(SYSC_IDLE_SMART_WKUP))
		*idlemodes |= MSTANDBY_SMART_WKUP;

	if (data->cfg->sidlemodes & BIT(SYSC_IDLE_FORCE))
		*idlemodes |= SIDLE_FORCE;
	if (data->cfg->sidlemodes & BIT(SYSC_IDLE_NO))
		*idlemodes |= SIDLE_NO;
	if (data->cfg->sidlemodes & BIT(SYSC_IDLE_SMART))
		*idlemodes |= SIDLE_SMART;
	if (data->cfg->sidlemodes & BIT(SYSC_IDLE_SMART_WKUP))
		*idlemodes |= SIDLE_SMART_WKUP;

	return 0;
}

/**
 * omap_hwmod_check_module - check new module against platform data
 * @dev: struct device
 * @oh: module
 * @data: new module data
 * @sysc_fields: sysc register bits
 * @rev_offs: revision register offset
 * @sysc_offs: sysconfig register offset
 * @syss_offs: sysstatus register offset
 * @sysc_flags: sysc specific flags
 * @idlemodes: sysc supported idlemodes
 */
static int omap_hwmod_check_module(struct device *dev,
				   struct omap_hwmod *oh,
				   const struct ti_sysc_module_data *data,
				   struct sysc_regbits *sysc_fields,
				   s32 rev_offs, s32 sysc_offs,
				   s32 syss_offs, u32 sysc_flags,
				   u32 idlemodes)
{
	if (!oh->class->sysc)
		return -ENODEV;

	if (oh->class->sysc->sysc_fields &&
	    sysc_fields != oh->class->sysc->sysc_fields)
		dev_warn(dev, "sysc_fields mismatch\n");

	if (rev_offs != oh->class->sysc->rev_offs)
		dev_warn(dev, "rev_offs %08x != %08x\n", rev_offs,
			 oh->class->sysc->rev_offs);
	if (sysc_offs != oh->class->sysc->sysc_offs)
		dev_warn(dev, "sysc_offs %08x != %08x\n", sysc_offs,
			 oh->class->sysc->sysc_offs);
	if (syss_offs != oh->class->sysc->syss_offs)
		dev_warn(dev, "syss_offs %08x != %08x\n", syss_offs,
			 oh->class->sysc->syss_offs);

	if (sysc_flags != oh->class->sysc->sysc_flags)
		dev_warn(dev, "sysc_flags %08x != %08x\n", sysc_flags,
			 oh->class->sysc->sysc_flags);

	if (idlemodes != oh->class->sysc->idlemodes)
		dev_warn(dev, "idlemodes %08x != %08x\n", idlemodes,
			 oh->class->sysc->idlemodes);

	if (data->cfg->srst_udelay != oh->class->sysc->srst_udelay)
		dev_warn(dev, "srst_udelay %i != %i\n",
			 data->cfg->srst_udelay,
			 oh->class->sysc->srst_udelay);

	return 0;
}

/**
 * omap_hwmod_allocate_module - allocate new module
 * @dev: struct device
 * @oh: module
 * @sysc_fields: sysc register bits
 * @clockdomain: clockdomain
 * @rev_offs: revision register offset
 * @sysc_offs: sysconfig register offset
 * @syss_offs: sysstatus register offset
 * @sysc_flags: sysc specific flags
 * @idlemodes: sysc supported idlemodes
 *
 * Note that the allocations here cannot use devm as ti-sysc can rebind.
 */
static int omap_hwmod_allocate_module(struct device *dev, struct omap_hwmod *oh,
				      const struct ti_sysc_module_data *data,
				      struct sysc_regbits *sysc_fields,
				      struct clockdomain *clkdm,
				      s32 rev_offs, s32 sysc_offs,
				      s32 syss_offs, u32 sysc_flags,
				      u32 idlemodes)
{
	struct omap_hwmod_class_sysconfig *sysc;
	struct omap_hwmod_class *class = NULL;
	struct omap_hwmod_ocp_if *oi = NULL;
	void __iomem *regs = NULL;
	unsigned long flags;

	sysc = kzalloc(sizeof(*sysc), GFP_KERNEL);
	if (!sysc)
		return -ENOMEM;

	sysc->sysc_fields = sysc_fields;
	sysc->rev_offs = rev_offs;
	sysc->sysc_offs = sysc_offs;
	sysc->syss_offs = syss_offs;
	sysc->sysc_flags = sysc_flags;
	sysc->idlemodes = idlemodes;
	sysc->srst_udelay = data->cfg->srst_udelay;

	if (!oh->_mpu_rt_va) {
		regs = ioremap(data->module_pa,
			       data->module_size);
		if (!regs)
			goto out_free_sysc;
	}

	/*
	 * We may need a new oh->class as the other devices in the same class
	 * may not yet have ioremapped their registers.
	 */
	if (oh->class->name && strcmp(oh->class->name, data->name)) {
		class = kmemdup(oh->class, sizeof(*oh->class), GFP_KERNEL);
		if (!class)
			goto out_unmap;
	}

	if (list_empty(&oh->slave_ports)) {
		oi = kcalloc(1, sizeof(*oi), GFP_KERNEL);
		if (!oi)
			goto out_free_class;

		/*
		 * Note that we assume interconnect interface clocks will be
		 * managed by the interconnect driver for OCPIF_SWSUP_IDLE case
		 * on omap24xx and omap3.
		 */
		oi->slave = oh;
		oi->user = OCP_USER_MPU | OCP_USER_SDMA;
	}

	spin_lock_irqsave(&oh->_lock, flags);
	if (regs)
		oh->_mpu_rt_va = regs;
	if (class)
		oh->class = class;
	oh->class->sysc = sysc;
	if (oi)
		_add_link(oi);
	if (clkdm)
		oh->clkdm = clkdm;
	oh->_state = _HWMOD_STATE_INITIALIZED;
	oh->_postsetup_state = _HWMOD_STATE_DEFAULT;
	_setup(oh, NULL);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return 0;

out_free_class:
	kfree(class);
out_unmap:
	iounmap(regs);
out_free_sysc:
	kfree(sysc);
	return -ENOMEM;
}

static const struct omap_hwmod_reset omap24xx_reset_quirks[] = {
	{ .match = "msdi", .len = 4, .reset = omap_msdi_reset, },
};

static const struct omap_hwmod_reset omap_reset_quirks[] = {
	{ .match = "dss_core", .len = 8, .reset = omap_dss_reset, },
	{ .match = "hdq1w", .len = 5, .reset = omap_hdq1w_reset, },
	{ .match = "i2c", .len = 3, .reset = omap_i2c_reset, },
	{ .match = "wd_timer", .len = 8, .reset = omap2_wd_timer_reset, },
};

static void
omap_hwmod_init_reset_quirk(struct device *dev, struct omap_hwmod *oh,
			    const struct ti_sysc_module_data *data,
			    const struct omap_hwmod_reset *quirks,
			    int quirks_sz)
{
	const struct omap_hwmod_reset *quirk;
	int i;

	for (i = 0; i < quirks_sz; i++) {
		quirk = &quirks[i];
		if (!strncmp(data->name, quirk->match, quirk->len)) {
			oh->class->reset = quirk->reset;

			return;
		}
	}
}

static void
omap_hwmod_init_reset_quirks(struct device *dev, struct omap_hwmod *oh,
			     const struct ti_sysc_module_data *data)
{
	if (soc_is_omap24xx())
		omap_hwmod_init_reset_quirk(dev, oh, data,
					    omap24xx_reset_quirks,
					    ARRAY_SIZE(omap24xx_reset_quirks));

	omap_hwmod_init_reset_quirk(dev, oh, data, omap_reset_quirks,
				    ARRAY_SIZE(omap_reset_quirks));
}

/**
 * omap_hwmod_init_module - initialize new module
 * @dev: struct device
 * @data: module data
 * @cookie: cookie for the caller to use for later calls
 */
int omap_hwmod_init_module(struct device *dev,
			   const struct ti_sysc_module_data *data,
			   struct ti_sysc_cookie *cookie)
{
	struct omap_hwmod *oh;
	struct sysc_regbits *sysc_fields;
	s32 rev_offs, sysc_offs, syss_offs;
	u32 sysc_flags, idlemodes;
	int error;

	if (!dev || !data || !data->name || !cookie)
		return -EINVAL;

	oh = _lookup(data->name);
	if (!oh) {
		oh = kzalloc(sizeof(*oh), GFP_KERNEL);
		if (!oh)
			return -ENOMEM;

		oh->name = data->name;
		oh->_state = _HWMOD_STATE_UNKNOWN;
		lockdep_register_key(&oh->hwmod_key);

		/* Unused, can be handled by PRM driver handling resets */
		oh->prcm.omap4.flags = HWMOD_OMAP4_NO_CONTEXT_LOSS_BIT;

		oh->class = kzalloc(sizeof(*oh->class), GFP_KERNEL);
		if (!oh->class) {
			kfree(oh);
			return -ENOMEM;
		}

		omap_hwmod_init_reset_quirks(dev, oh, data);

		oh->class->name = data->name;
		mutex_lock(&list_lock);
		error = _register(oh);
		mutex_unlock(&list_lock);
	}

	cookie->data = oh;

	error = omap_hwmod_init_regbits(dev, oh, data, &sysc_fields);
	if (error)
		return error;

	error = omap_hwmod_init_reg_offs(dev, data, &rev_offs,
					 &sysc_offs, &syss_offs);
	if (error)
		return error;

	error = omap_hwmod_init_sysc_flags(dev, data, &sysc_flags);
	if (error)
		return error;

	error = omap_hwmod_init_idlemodes(dev, data, &idlemodes);
	if (error)
		return error;

	if (data->cfg->quirks & SYSC_QUIRK_NO_IDLE)
		oh->flags |= HWMOD_NO_IDLE;
	if (data->cfg->quirks & SYSC_QUIRK_NO_IDLE_ON_INIT)
		oh->flags |= HWMOD_INIT_NO_IDLE;
	if (data->cfg->quirks & SYSC_QUIRK_NO_RESET_ON_INIT)
		oh->flags |= HWMOD_INIT_NO_RESET;
	if (data->cfg->quirks & SYSC_QUIRK_USE_CLOCKACT)
		oh->flags |= HWMOD_SET_DEFAULT_CLOCKACT;
	if (data->cfg->quirks & SYSC_QUIRK_SWSUP_SIDLE)
		oh->flags |= HWMOD_SWSUP_SIDLE;
	if (data->cfg->quirks & SYSC_QUIRK_SWSUP_SIDLE_ACT)
		oh->flags |= HWMOD_SWSUP_SIDLE_ACT;
	if (data->cfg->quirks & SYSC_QUIRK_SWSUP_MSTANDBY)
		oh->flags |= HWMOD_SWSUP_MSTANDBY;
	if (data->cfg->quirks & SYSC_QUIRK_CLKDM_NOAUTO)
		oh->flags |= HWMOD_CLKDM_NOAUTO;

	error = omap_hwmod_check_module(dev, oh, data, sysc_fields,
					rev_offs, sysc_offs, syss_offs,
					sysc_flags, idlemodes);
	if (!error)
		return error;

	return omap_hwmod_allocate_module(dev, oh, data, sysc_fields,
					  cookie->clkdm, rev_offs,
					  sysc_offs, syss_offs,
					  sysc_flags, idlemodes);
}

/**
 * omap_hwmod_setup_earlycon_flags - set up flags for early console
 *
 * Enable DEBUG_OMAPUART_FLAGS for uart hwmod that is being used as
 * early concole so that hwmod core doesn't reset and keep it in idle
 * that specific uart.
 */
#ifdef CONFIG_SERIAL_EARLYCON
static void __init omap_hwmod_setup_earlycon_flags(void)
{
	struct device_node *np;
	struct omap_hwmod *oh;
	const char *uart;

	np = of_find_node_by_path("/chosen");
	if (np) {
		uart = of_get_property(np, "stdout-path", NULL);
		if (uart) {
			np = of_find_node_by_path(uart);
			if (np) {
				uart = of_get_property(np, "ti,hwmods", NULL);
				oh = omap_hwmod_lookup(uart);
				if (!oh) {
					uart = of_get_property(np->parent,
							       "ti,hwmods",
							       NULL);
					oh = omap_hwmod_lookup(uart);
				}
				if (oh)
					oh->flags |= DEBUG_OMAPUART_FLAGS;
			}
		}
	}
}
#endif

/**
 * omap_hwmod_setup_all - set up all registered IP blocks
 *
 * Initialize and set up all IP blocks registered with the hwmod code.
 * Must be called after omap2_clk_init().  Resolves the struct clk
 * names to struct clk pointers for each registered omap_hwmod.  Also
 * calls _setup() on each hwmod.  Returns 0 upon success.
 */
static int __init omap_hwmod_setup_all(void)
{
	if (!inited)
		return 0;

	_ensure_mpu_hwmod_is_setup(NULL);

	omap_hwmod_for_each(_init, NULL);
#ifdef CONFIG_SERIAL_EARLYCON
	omap_hwmod_setup_earlycon_flags();
#endif
	omap_hwmod_for_each(_setup, NULL);

	return 0;
}
omap_postcore_initcall(omap_hwmod_setup_all);

/**
 * omap_hwmod_enable - enable an omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Enable an omap_hwmod @oh.  Intended to be called by omap_device_enable().
 * Returns -EINVAL on error or passes along the return value from _enable().
 */
int omap_hwmod_enable(struct omap_hwmod *oh)
{
	int r;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	r = _enable(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return r;
}

/**
 * omap_hwmod_idle - idle an omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Idle an omap_hwmod @oh.  Intended to be called by omap_device_idle().
 * Returns -EINVAL on error or passes along the return value from _idle().
 */
int omap_hwmod_idle(struct omap_hwmod *oh)
{
	int r;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	r = _idle(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return r;
}

/**
 * omap_hwmod_shutdown - shutdown an omap_hwmod
 * @oh: struct omap_hwmod *
 *
 * Shutdown an omap_hwmod @oh.  Intended to be called by
 * omap_device_shutdown().  Returns -EINVAL on error or passes along
 * the return value from _shutdown().
 */
int omap_hwmod_shutdown(struct omap_hwmod *oh)
{
	int r;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	r = _shutdown(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return r;
}

/*
 * IP block data retrieval functions
 */

/**
 * omap_hwmod_get_pwrdm - return pointer to this module's main powerdomain
 * @oh: struct omap_hwmod *
 *
 * Return the powerdomain pointer associated with the OMAP module
 * @oh's main clock.  If @oh does not have a main clk, return the
 * powerdomain associated with the interface clock associated with the
 * module's MPU port. (XXX Perhaps this should use the SDMA port
 * instead?)  Returns NULL on error, or a struct powerdomain * on
 * success.
 */
struct powerdomain *omap_hwmod_get_pwrdm(struct omap_hwmod *oh)
{
	struct clk *c;
	struct omap_hwmod_ocp_if *oi;
	struct clockdomain *clkdm;
	struct clk_hw_omap *clk;
	struct clk_hw *hw;

	if (!oh)
		return NULL;

	if (oh->clkdm)
		return oh->clkdm->pwrdm.ptr;

	if (oh->_clk) {
		c = oh->_clk;
	} else {
		oi = _find_mpu_rt_port(oh);
		if (!oi)
			return NULL;
		c = oi->_clk;
	}

	hw = __clk_get_hw(c);
	if (!hw)
		return NULL;

	clk = to_clk_hw_omap(hw);
	if (!clk)
		return NULL;

	clkdm = clk->clkdm;
	if (!clkdm)
		return NULL;

	return clkdm->pwrdm.ptr;
}

/**
 * omap_hwmod_get_mpu_rt_va - return the module's base address (for the MPU)
 * @oh: struct omap_hwmod *
 *
 * Returns the virtual address corresponding to the beginning of the
 * module's register target, in the address range that is intended to
 * be used by the MPU.  Returns the virtual address upon success or NULL
 * upon error.
 */
void __iomem *omap_hwmod_get_mpu_rt_va(struct omap_hwmod *oh)
{
	if (!oh)
		return NULL;

	if (oh->_int_flags & _HWMOD_NO_MPU_PORT)
		return NULL;

	if (oh->_state == _HWMOD_STATE_UNKNOWN)
		return NULL;

	return oh->_mpu_rt_va;
}

/*
 * XXX what about functions for drivers to save/restore ocp_sysconfig
 * for context save/restore operations?
 */

/**
 * omap_hwmod_assert_hardreset - assert the HW reset line of submodules
 * contained in the hwmod module.
 * @oh: struct omap_hwmod *
 * @name: name of the reset line to lookup and assert
 *
 * Some IP like dsp, ipu or iva contain processor that require
 * an HW reset line to be assert / deassert in order to enable fully
 * the IP.  Returns -EINVAL if @oh is null or if the operation is not
 * yet supported on this OMAP; otherwise, passes along the return value
 * from _assert_hardreset().
 */
int omap_hwmod_assert_hardreset(struct omap_hwmod *oh, const char *name)
{
	int ret;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	ret = _assert_hardreset(oh, name);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return ret;
}

/**
 * omap_hwmod_deassert_hardreset - deassert the HW reset line of submodules
 * contained in the hwmod module.
 * @oh: struct omap_hwmod *
 * @name: name of the reset line to look up and deassert
 *
 * Some IP like dsp, ipu or iva contain processor that require
 * an HW reset line to be assert / deassert in order to enable fully
 * the IP.  Returns -EINVAL if @oh is null or if the operation is not
 * yet supported on this OMAP; otherwise, passes along the return value
 * from _deassert_hardreset().
 */
int omap_hwmod_deassert_hardreset(struct omap_hwmod *oh, const char *name)
{
	int ret;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	ret = _deassert_hardreset(oh, name);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return ret;
}

/**
 * omap_hwmod_for_each_by_class - call @fn for each hwmod of class @classname
 * @classname: struct omap_hwmod_class name to search for
 * @fn: callback function pointer to call for each hwmod in class @classname
 * @user: arbitrary context data to pass to the callback function
 *
 * For each omap_hwmod of class @classname, call @fn.
 * If the callback function returns something other than
 * zero, the iterator is terminated, and the callback function's return
 * value is passed back to the caller.  Returns 0 upon success, -EINVAL
 * if @classname or @fn are NULL, or passes back the error code from @fn.
 */
int omap_hwmod_for_each_by_class(const char *classname,
				 int (*fn)(struct omap_hwmod *oh,
					   void *user),
				 void *user)
{
	struct omap_hwmod *temp_oh;
	int ret = 0;

	if (!classname || !fn)
		return -EINVAL;

	pr_debug("omap_hwmod: %s: looking for modules of class %s\n",
		 __func__, classname);

	list_for_each_entry(temp_oh, &omap_hwmod_list, node) {
		if (!strcmp(temp_oh->class->name, classname)) {
			pr_debug("omap_hwmod: %s: %s: calling callback fn\n",
				 __func__, temp_oh->name);
			ret = (*fn)(temp_oh, user);
			if (ret)
				break;
		}
	}

	if (ret)
		pr_debug("omap_hwmod: %s: iterator terminated early: %d\n",
			 __func__, ret);

	return ret;
}

/**
 * omap_hwmod_set_postsetup_state - set the post-_setup() state for this hwmod
 * @oh: struct omap_hwmod *
 * @state: state that _setup() should leave the hwmod in
 *
 * Sets the hwmod state that @oh will enter at the end of _setup()
 * (called by omap_hwmod_setup_*()).  See also the documentation
 * for _setup_postsetup(), above.  Returns 0 upon success or
 * -EINVAL if there is a problem with the arguments or if the hwmod is
 * in the wrong state.
 */
int omap_hwmod_set_postsetup_state(struct omap_hwmod *oh, u8 state)
{
	int ret;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	if (state != _HWMOD_STATE_DISABLED &&
	    state != _HWMOD_STATE_ENABLED &&
	    state != _HWMOD_STATE_IDLE)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);

	if (oh->_state != _HWMOD_STATE_REGISTERED) {
		ret = -EINVAL;
		goto ohsps_unlock;
	}

	oh->_postsetup_state = state;
	ret = 0;

ohsps_unlock:
	spin_unlock_irqrestore(&oh->_lock, flags);

	return ret;
}

/**
 * omap_hwmod_get_context_loss_count - get lost context count
 * @oh: struct omap_hwmod *
 *
 * Returns the context loss count of associated @oh
 * upon success, or zero if no context loss data is available.
 *
 * On OMAP4, this queries the per-hwmod context loss register,
 * assuming one exists.  If not, or on OMAP2/3, this queries the
 * enclosing powerdomain context loss count.
 */
int omap_hwmod_get_context_loss_count(struct omap_hwmod *oh)
{
	struct powerdomain *pwrdm;
	int ret = 0;

	if (soc_ops.get_context_lost)
		return soc_ops.get_context_lost(oh);

	pwrdm = omap_hwmod_get_pwrdm(oh);
	if (pwrdm)
		ret = pwrdm_get_context_loss_count(pwrdm);

	return ret;
}

/**
 * omap_hwmod_init - initialize the hwmod code
 *
 * Sets up some function pointers needed by the hwmod code to operate on the
 * currently-booted SoC.  Intended to be called once during kernel init
 * before any hwmods are registered.  No return value.
 */
void __init omap_hwmod_init(void)
{
	if (cpu_is_omap24xx()) {
		soc_ops.wait_target_ready = _omap2xxx_3xxx_wait_target_ready;
		soc_ops.assert_hardreset = _omap2_assert_hardreset;
		soc_ops.deassert_hardreset = _omap2_deassert_hardreset;
		soc_ops.is_hardreset_asserted = _omap2_is_hardreset_asserted;
	} else if (cpu_is_omap34xx()) {
		soc_ops.wait_target_ready = _omap2xxx_3xxx_wait_target_ready;
		soc_ops.assert_hardreset = _omap2_assert_hardreset;
		soc_ops.deassert_hardreset = _omap2_deassert_hardreset;
		soc_ops.is_hardreset_asserted = _omap2_is_hardreset_asserted;
		soc_ops.init_clkdm = _init_clkdm;
	} else if (cpu_is_omap44xx() || soc_is_omap54xx() || soc_is_dra7xx()) {
		soc_ops.enable_module = _omap4_enable_module;
		soc_ops.disable_module = _omap4_disable_module;
		soc_ops.wait_target_ready = _omap4_wait_target_ready;
		soc_ops.assert_hardreset = _omap4_assert_hardreset;
		soc_ops.deassert_hardreset = _omap4_deassert_hardreset;
		soc_ops.is_hardreset_asserted = _omap4_is_hardreset_asserted;
		soc_ops.init_clkdm = _init_clkdm;
		soc_ops.update_context_lost = _omap4_update_context_lost;
		soc_ops.get_context_lost = _omap4_get_context_lost;
		soc_ops.disable_direct_prcm = _omap4_disable_direct_prcm;
		soc_ops.xlate_clkctrl = _omap4_xlate_clkctrl;
	} else if (cpu_is_ti814x() || cpu_is_ti816x() || soc_is_am33xx() ||
		   soc_is_am43xx()) {
		soc_ops.enable_module = _omap4_enable_module;
		soc_ops.disable_module = _omap4_disable_module;
		soc_ops.wait_target_ready = _omap4_wait_target_ready;
		soc_ops.assert_hardreset = _omap4_assert_hardreset;
		soc_ops.deassert_hardreset = _am33xx_deassert_hardreset;
		soc_ops.is_hardreset_asserted = _omap4_is_hardreset_asserted;
		soc_ops.init_clkdm = _init_clkdm;
		soc_ops.disable_direct_prcm = _omap4_disable_direct_prcm;
		soc_ops.xlate_clkctrl = _omap4_xlate_clkctrl;
	} else {
		WARN(1, "omap_hwmod: unknown SoC type\n");
	}

	_init_clkctrl_providers();

	inited = true;
}

/**
 * omap_hwmod_get_main_clk - get pointer to main clock name
 * @oh: struct omap_hwmod *
 *
 * Returns the main clock name assocated with @oh upon success,
 * or NULL if @oh is NULL.
 */
const char *omap_hwmod_get_main_clk(struct omap_hwmod *oh)
{
	if (!oh)
		return NULL;

	return oh->main_clk;
}
