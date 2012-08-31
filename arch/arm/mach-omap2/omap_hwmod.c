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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
 *            | (__raw_{read,write}l, clk*)   |
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
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/bootmem.h>

#include <plat/clock.h>
#include <plat/omap_hwmod.h>
#include <plat/prcm.h>

#include "soc.h"
#include "common.h"
#include "clockdomain.h"
#include "powerdomain.h"
#include "cm2xxx_3xxx.h"
#include "cminst44xx.h"
#include "prm2xxx_3xxx.h"
#include "prm44xx.h"
#include "prminst44xx.h"
#include "mux.h"
#include "pm.h"

/* Maximum microseconds to wait for OMAP module to softreset */
#define MAX_MODULE_SOFTRESET_WAIT	10000

/* Name of the OMAP hwmod for the MPU */
#define MPU_INITIATOR_NAME		"mpu"

/*
 * Number of struct omap_hwmod_link records per struct
 * omap_hwmod_ocp_if record (master->slave and slave->master)
 */
#define LINKS_PER_OCP_IF		2

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
};

/* soc_ops: adapts the omap_hwmod code to the currently-booted SoC */
static struct omap_hwmod_soc_ops soc_ops;

/* omap_hwmod_list contains all registered struct omap_hwmods */
static LIST_HEAD(omap_hwmod_list);

/* mpu_oh: used to add/remove MPU initiator from sleepdep list */
static struct omap_hwmod *mpu_oh;

/* io_chain_lock: used to serialize reconfigurations of the I/O chain */
static DEFINE_SPINLOCK(io_chain_lock);

/*
 * linkspace: ptr to a buffer that struct omap_hwmod_link records are
 * allocated from - used to reduce the number of small memory
 * allocations, which has a significant impact on performance
 */
static struct omap_hwmod_link *linkspace;

/*
 * free_ls, max_ls: array indexes into linkspace; representing the
 * next free struct omap_hwmod_link index, and the maximum number of
 * struct omap_hwmod_link records allocated (respectively)
 */
static unsigned short free_ls, max_ls, ls_supp;

/* inited: set to true once the hwmod code is initialized */
static bool inited;

/* Private functions */

/**
 * _fetch_next_ocp_if - return the next OCP interface in a list
 * @p: ptr to a ptr to the list_head inside the ocp_if to return
 * @i: pointer to the index of the element pointed to by @p in the list
 *
 * Return a pointer to the struct omap_hwmod_ocp_if record
 * containing the struct list_head pointed to by @p, and increment
 * @p such that a future call to this routine will return the next
 * record.
 */
static struct omap_hwmod_ocp_if *_fetch_next_ocp_if(struct list_head **p,
						    int *i)
{
	struct omap_hwmod_ocp_if *oi;

	oi = list_entry(*p, struct omap_hwmod_link, node)->ocp_if;
	*p = (*p)->next;

	*i = *i + 1;

	return oi;
}

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
	omap_hwmod_write(v, oh, oh->class->sysc->sysc_offs);
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
 * _set_softreset: set OCP_SYSCONFIG.CLOCKACTIVITY bits in @v
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
 * _set_idle_ioring_wakeup - enable/disable IO pad wakeup on hwmod idle for mux
 * @oh: struct omap_hwmod *
 * @set_wake: bool value indicating to set (true) or clear (false) wakeup enable
 *
 * Set or clear the I/O pad wakeup flag in the mux entries for the
 * hwmod @oh.  This function changes the @oh->mux->pads_dynamic array
 * in memory.  If the hwmod is currently idled, and the new idle
 * values don't match the previous ones, this function will also
 * update the SCM PADCTRL registers.  Otherwise, if the hwmod is not
 * currently idled, this function won't touch the hardware: the new
 * mux settings are written to the SCM PADCTRL registers when the
 * hwmod is idled.  No return value.
 */
static void _set_idle_ioring_wakeup(struct omap_hwmod *oh, bool set_wake)
{
	struct omap_device_pad *pad;
	bool change = false;
	u16 prev_idle;
	int j;

	if (!oh->mux || !oh->mux->enabled)
		return;

	for (j = 0; j < oh->mux->nr_pads_dynamic; j++) {
		pad = oh->mux->pads_dynamic[j];

		if (!(pad->flags & OMAP_DEVICE_PAD_WAKEUP))
			continue;

		prev_idle = pad->idle;

		if (set_wake)
			pad->idle |= OMAP_WAKEUP_EN;
		else
			pad->idle &= ~OMAP_WAKEUP_EN;

		if (prev_idle != pad->idle)
			change = true;
	}

	if (change && oh->_state == _HWMOD_STATE_IDLE)
		omap_hwmod_mux(oh->mux, _HWMOD_STATE_IDLE);
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

	oh->_int_flags |= _HWMOD_WAKEUP_ENABLED;

	return 0;
}

/**
 * _disable_wakeup: clear OCP_SYSCONFIG.ENAWAKEUP bit in the hardware
 * @oh: struct omap_hwmod *
 *
 * Prevent the hardware module @oh to send wakeups.  Returns -EINVAL
 * upon error or 0 upon success.
 */
static int _disable_wakeup(struct omap_hwmod *oh, u32 *v)
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
		*v &= ~(0x1 << oh->class->sysc->sysc_fields->enwkup_shift);

	if (oh->class->sysc->idlemodes & SIDLE_SMART_WKUP)
		_set_slave_idlemode(oh, HWMOD_IDLEMODE_SMART, v);
	if (oh->class->sysc->idlemodes & MSTANDBY_SMART_WKUP)
		_set_master_standbymode(oh, HWMOD_IDLEMODE_SMART, v);

	/* XXX test pwrdm_get_wken for this hwmod's subsystem */

	oh->_int_flags &= ~_HWMOD_WAKEUP_ENABLED;

	return 0;
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
	if (!oh->_clk)
		return -EINVAL;

	if (oh->_clk->clkdm && oh->_clk->clkdm->flags & CLKDM_NO_AUTODEPS)
		return 0;

	return clkdm_add_sleepdep(oh->_clk->clkdm, init_oh->_clk->clkdm);
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
	if (!oh->_clk)
		return -EINVAL;

	if (oh->_clk->clkdm && oh->_clk->clkdm->flags & CLKDM_NO_AUTODEPS)
		return 0;

	return clkdm_del_sleepdep(oh->_clk->clkdm, init_oh->_clk->clkdm);
}

/**
 * _init_main_clk - get a struct clk * for the the hwmod's main functional clk
 * @oh: struct omap_hwmod *
 *
 * Called from _init_clocks().  Populates the @oh _clk (main
 * functional clock pointer) if a main_clk is present.  Returns 0 on
 * success or -EINVAL on error.
 */
static int _init_main_clk(struct omap_hwmod *oh)
{
	int ret = 0;

	if (!oh->main_clk)
		return 0;

	oh->_clk = omap_clk_get_by_name(oh->main_clk);
	if (!oh->_clk) {
		pr_warning("omap_hwmod: %s: cannot clk_get main_clk %s\n",
			   oh->name, oh->main_clk);
		return -EINVAL;
	}

	if (!oh->_clk->clkdm)
		pr_warning("omap_hwmod: %s: missing clockdomain for %s.\n",
			   oh->main_clk, oh->_clk->name);

	return ret;
}

/**
 * _init_interface_clks - get a struct clk * for the the hwmod's interface clks
 * @oh: struct omap_hwmod *
 *
 * Called from _init_clocks().  Populates the @oh OCP slave interface
 * clock pointers.  Returns 0 on success or -EINVAL on error.
 */
static int _init_interface_clks(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;
	struct list_head *p;
	struct clk *c;
	int i = 0;
	int ret = 0;

	p = oh->slave_ports.next;

	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);
		if (!os->clk)
			continue;

		c = omap_clk_get_by_name(os->clk);
		if (!c) {
			pr_warning("omap_hwmod: %s: cannot clk_get interface_clk %s\n",
				   oh->name, os->clk);
			ret = -EINVAL;
		}
		os->_clk = c;
	}

	return ret;
}

/**
 * _init_opt_clk - get a struct clk * for the the hwmod's optional clocks
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
		c = omap_clk_get_by_name(oc->clk);
		if (!c) {
			pr_warning("omap_hwmod: %s: cannot clk_get opt_clk %s\n",
				   oh->name, oc->clk);
			ret = -EINVAL;
		}
		oc->_clk = c;
	}

	return ret;
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
	struct list_head *p;
	int i = 0;

	pr_debug("omap_hwmod: %s: enabling clocks\n", oh->name);

	if (oh->_clk)
		clk_enable(oh->_clk);

	p = oh->slave_ports.next;

	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);

		if (os->_clk && (os->flags & OCPIF_SWSUP_IDLE))
			clk_enable(os->_clk);
	}

	/* The opt clocks are controlled by the device driver. */

	return 0;
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
	struct list_head *p;
	int i = 0;

	pr_debug("omap_hwmod: %s: disabling clocks\n", oh->name);

	if (oh->_clk)
		clk_disable(oh->_clk);

	p = oh->slave_ports.next;

	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);

		if (os->_clk && (os->flags & OCPIF_SWSUP_IDLE))
			clk_disable(os->_clk);
	}

	/* The opt clocks are controlled by the device driver. */

	return 0;
}

static void _enable_optional_clocks(struct omap_hwmod *oh)
{
	struct omap_hwmod_opt_clk *oc;
	int i;

	pr_debug("omap_hwmod: %s: enabling optional clocks\n", oh->name);

	for (i = oh->opt_clks_cnt, oc = oh->opt_clks; i > 0; i--, oc++)
		if (oc->_clk) {
			pr_debug("omap_hwmod: enable %s:%s\n", oc->role,
				 oc->_clk->name);
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
				 oc->_clk->name);
			clk_disable(oc->_clk);
		}
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
	if (!oh->clkdm || !oh->prcm.omap4.modulemode)
		return;

	pr_debug("omap_hwmod: %s: %s: %d\n",
		 oh->name, __func__, oh->prcm.omap4.modulemode);

	omap4_cminst_module_enable(oh->prcm.omap4.modulemode,
				   oh->clkdm->prcm_partition,
				   oh->clkdm->cm_inst,
				   oh->clkdm->clkdm_offs,
				   oh->prcm.omap4.clkctrl_offs);
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
	if (!oh || !oh->clkdm)
		return -EINVAL;

	if (oh->_int_flags & _HWMOD_NO_MPU_PORT)
		return 0;

	if (oh->flags & HWMOD_NO_IDLEST)
		return 0;

	return omap4_cminst_wait_module_idle(oh->clkdm->prcm_partition,
					     oh->clkdm->cm_inst,
					     oh->clkdm->clkdm_offs,
					     oh->prcm.omap4.clkctrl_offs);
}

/**
 * _count_mpu_irqs - count the number of MPU IRQ lines associated with @oh
 * @oh: struct omap_hwmod *oh
 *
 * Count and return the number of MPU IRQs associated with the hwmod
 * @oh.  Used to allocate struct resource data.  Returns 0 if @oh is
 * NULL.
 */
static int _count_mpu_irqs(struct omap_hwmod *oh)
{
	struct omap_hwmod_irq_info *ohii;
	int i = 0;

	if (!oh || !oh->mpu_irqs)
		return 0;

	do {
		ohii = &oh->mpu_irqs[i++];
	} while (ohii->irq != -1);

	return i-1;
}

/**
 * _count_sdma_reqs - count the number of SDMA request lines associated with @oh
 * @oh: struct omap_hwmod *oh
 *
 * Count and return the number of SDMA request lines associated with
 * the hwmod @oh.  Used to allocate struct resource data.  Returns 0
 * if @oh is NULL.
 */
static int _count_sdma_reqs(struct omap_hwmod *oh)
{
	struct omap_hwmod_dma_info *ohdi;
	int i = 0;

	if (!oh || !oh->sdma_reqs)
		return 0;

	do {
		ohdi = &oh->sdma_reqs[i++];
	} while (ohdi->dma_req != -1);

	return i-1;
}

/**
 * _count_ocp_if_addr_spaces - count the number of address space entries for @oh
 * @oh: struct omap_hwmod *oh
 *
 * Count and return the number of address space ranges associated with
 * the hwmod @oh.  Used to allocate struct resource data.  Returns 0
 * if @oh is NULL.
 */
static int _count_ocp_if_addr_spaces(struct omap_hwmod_ocp_if *os)
{
	struct omap_hwmod_addr_space *mem;
	int i = 0;

	if (!os || !os->addr)
		return 0;

	do {
		mem = &os->addr[i++];
	} while (mem->pa_start != mem->pa_end);

	return i-1;
}

/**
 * _get_mpu_irq_by_name - fetch MPU interrupt line number by name
 * @oh: struct omap_hwmod * to operate on
 * @name: pointer to the name of the MPU interrupt number to fetch (optional)
 * @irq: pointer to an unsigned int to store the MPU IRQ number to
 *
 * Retrieve a MPU hardware IRQ line number named by @name associated
 * with the IP block pointed to by @oh.  The IRQ number will be filled
 * into the address pointed to by @dma.  When @name is non-null, the
 * IRQ line number associated with the named entry will be returned.
 * If @name is null, the first matching entry will be returned.  Data
 * order is not meaningful in hwmod data, so callers are strongly
 * encouraged to use a non-null @name whenever possible to avoid
 * unpredictable effects if hwmod data is later added that causes data
 * ordering to change.  Returns 0 upon success or a negative error
 * code upon error.
 */
static int _get_mpu_irq_by_name(struct omap_hwmod *oh, const char *name,
				unsigned int *irq)
{
	int i;
	bool found = false;

	if (!oh->mpu_irqs)
		return -ENOENT;

	i = 0;
	while (oh->mpu_irqs[i].irq != -1) {
		if (name == oh->mpu_irqs[i].name ||
		    !strcmp(name, oh->mpu_irqs[i].name)) {
			found = true;
			break;
		}
		i++;
	}

	if (!found)
		return -ENOENT;

	*irq = oh->mpu_irqs[i].irq;

	return 0;
}

/**
 * _get_sdma_req_by_name - fetch SDMA request line ID by name
 * @oh: struct omap_hwmod * to operate on
 * @name: pointer to the name of the SDMA request line to fetch (optional)
 * @dma: pointer to an unsigned int to store the request line ID to
 *
 * Retrieve an SDMA request line ID named by @name on the IP block
 * pointed to by @oh.  The ID will be filled into the address pointed
 * to by @dma.  When @name is non-null, the request line ID associated
 * with the named entry will be returned.  If @name is null, the first
 * matching entry will be returned.  Data order is not meaningful in
 * hwmod data, so callers are strongly encouraged to use a non-null
 * @name whenever possible to avoid unpredictable effects if hwmod
 * data is later added that causes data ordering to change.  Returns 0
 * upon success or a negative error code upon error.
 */
static int _get_sdma_req_by_name(struct omap_hwmod *oh, const char *name,
				 unsigned int *dma)
{
	int i;
	bool found = false;

	if (!oh->sdma_reqs)
		return -ENOENT;

	i = 0;
	while (oh->sdma_reqs[i].dma_req != -1) {
		if (name == oh->sdma_reqs[i].name ||
		    !strcmp(name, oh->sdma_reqs[i].name)) {
			found = true;
			break;
		}
		i++;
	}

	if (!found)
		return -ENOENT;

	*dma = oh->sdma_reqs[i].dma_req;

	return 0;
}

/**
 * _get_addr_space_by_name - fetch address space start & end by name
 * @oh: struct omap_hwmod * to operate on
 * @name: pointer to the name of the address space to fetch (optional)
 * @pa_start: pointer to a u32 to store the starting address to
 * @pa_end: pointer to a u32 to store the ending address to
 *
 * Retrieve address space start and end addresses for the IP block
 * pointed to by @oh.  The data will be filled into the addresses
 * pointed to by @pa_start and @pa_end.  When @name is non-null, the
 * address space data associated with the named entry will be
 * returned.  If @name is null, the first matching entry will be
 * returned.  Data order is not meaningful in hwmod data, so callers
 * are strongly encouraged to use a non-null @name whenever possible
 * to avoid unpredictable effects if hwmod data is later added that
 * causes data ordering to change.  Returns 0 upon success or a
 * negative error code upon error.
 */
static int _get_addr_space_by_name(struct omap_hwmod *oh, const char *name,
				   u32 *pa_start, u32 *pa_end)
{
	int i, j;
	struct omap_hwmod_ocp_if *os;
	struct list_head *p = NULL;
	bool found = false;

	p = oh->slave_ports.next;

	i = 0;
	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);

		if (!os->addr)
			return -ENOENT;

		j = 0;
		while (os->addr[j].pa_start != os->addr[j].pa_end) {
			if (name == os->addr[j].name ||
			    !strcmp(name, os->addr[j].name)) {
				found = true;
				break;
			}
			j++;
		}

		if (found)
			break;
	}

	if (!found)
		return -ENOENT;

	*pa_start = os->addr[j].pa_start;
	*pa_end = os->addr[j].pa_end;

	return 0;
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
	struct list_head *p;
	int i = 0;

	if (!oh)
		return;

	oh->_int_flags |= _HWMOD_NO_MPU_PORT;

	p = oh->slave_ports.next;

	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);
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
 * _find_mpu_rt_addr_space - return MPU register target address space for @oh
 * @oh: struct omap_hwmod *
 *
 * Returns a pointer to the struct omap_hwmod_addr_space record representing
 * the register target MPU address space; or returns NULL upon error.
 */
static struct omap_hwmod_addr_space * __init _find_mpu_rt_addr_space(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;
	struct omap_hwmod_addr_space *mem;
	int found = 0, i = 0;

	os = _find_mpu_rt_port(oh);
	if (!os || !os->addr)
		return NULL;

	do {
		mem = &os->addr[i++];
		if (mem->flags & ADDR_TYPE_RT)
			found = 1;
	} while (!found && mem->pa_start != mem->pa_end);

	return (found) ? mem : NULL;
}

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

	if (!oh->class->sysc)
		return;

	v = oh->_sysc_cache;
	sf = oh->class->sysc->sysc_flags;

	if (sf & SYSC_HAS_SIDLEMODE) {
		clkdm_act = ((oh->clkdm &&
			      oh->clkdm->flags & CLKDM_ACTIVE_WITH_MPU) ||
			     (oh->_clk && oh->_clk->clkdm &&
			      oh->_clk->clkdm->flags & CLKDM_ACTIVE_WITH_MPU));
		if (clkdm_act && !(oh->class->sysc->idlemodes &
				   (SIDLE_SMART | SIDLE_SMART_WKUP)))
			idlemode = HWMOD_IDLEMODE_FORCE;
		else
			idlemode = (oh->flags & HWMOD_SWSUP_SIDLE) ?
				HWMOD_IDLEMODE_NO : HWMOD_IDLEMODE_SMART;
		_set_slave_idlemode(oh, idlemode, &v);
	}

	if (sf & SYSC_HAS_MIDLEMODE) {
		if (oh->flags & HWMOD_SWSUP_MSTANDBY) {
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
		_set_clockactivity(oh, oh->class->sysc->clockact, &v);

	/* If slave is in SMARTIDLE, also enable wakeup */
	if ((sf & SYSC_HAS_SIDLEMODE) && !(oh->flags & HWMOD_SWSUP_SIDLE))
		_enable_wakeup(oh, &v);

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
		/* XXX What about HWMOD_IDLEMODE_SMART_WKUP? */
		if (oh->flags & HWMOD_SWSUP_SIDLE ||
		    !(oh->class->sysc->idlemodes &
		      (SIDLE_SMART | SIDLE_SMART_WKUP)))
			idlemode = HWMOD_IDLEMODE_FORCE;
		else
			idlemode = HWMOD_IDLEMODE_SMART;
		_set_slave_idlemode(oh, idlemode, &v);
	}

	if (sf & SYSC_HAS_MIDLEMODE) {
		if (oh->flags & HWMOD_SWSUP_MSTANDBY) {
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

	/* If slave is in SMARTIDLE, also enable wakeup */
	if ((sf & SYSC_HAS_SIDLEMODE) && !(oh->flags & HWMOD_SWSUP_SIDLE))
		_enable_wakeup(oh, &v);

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
	if (!oh->clkdm_name)
		return 0;

	oh->clkdm = clkdm_lookup(oh->clkdm_name);
	if (!oh->clkdm) {
		pr_warning("omap_hwmod: %s: could not associate to clkdm %s\n",
			oh->name, oh->clkdm_name);
		return -EINVAL;
	}

	pr_debug("omap_hwmod: %s: associated to clkdm %s\n",
		oh->name, oh->clkdm_name);

	return 0;
}

/**
 * _init_clocks - clk_get() all clocks associated with this hwmod. Retrieve as
 * well the clockdomain.
 * @oh: struct omap_hwmod *
 * @data: not used; pass NULL
 *
 * Called by omap_hwmod_setup_*() (after omap2_clk_init()).
 * Resolves all clock names embedded in the hwmod.  Returns 0 on
 * success, or a negative error code on failure.
 */
static int _init_clocks(struct omap_hwmod *oh, void *data)
{
	int ret = 0;

	if (oh->_state != _HWMOD_STATE_REGISTERED)
		return 0;

	pr_debug("omap_hwmod: %s: looking up clocks\n", oh->name);

	ret |= _init_main_clk(oh);
	ret |= _init_interface_clks(oh);
	ret |= _init_opt_clks(oh);
	if (soc_ops.init_clkdm)
		ret |= soc_ops.init_clkdm(oh);

	if (!ret)
		oh->_state = _HWMOD_STATE_CLKS_INITED;
	else
		pr_warning("omap_hwmod: %s: cannot _init_clocks\n", oh->name);

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
static u8 _lookup_hardreset(struct omap_hwmod *oh, const char *name,
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
	u8 ret = -EINVAL;

	if (!oh)
		return -EINVAL;

	if (!soc_ops.assert_hardreset)
		return -ENOSYS;

	ret = _lookup_hardreset(oh, name, &ohri);
	if (IS_ERR_VALUE(ret))
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
	if (IS_ERR_VALUE(ret))
		return ret;

	ret = soc_ops.deassert_hardreset(oh, &ohri);
	if (ret == -EBUSY)
		pr_warning("omap_hwmod: %s: failed to hardreset\n", oh->name);

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
	u8 ret = -EINVAL;

	if (!oh)
		return -EINVAL;

	if (!soc_ops.is_hardreset_asserted)
		return -ENOSYS;

	ret = _lookup_hardreset(oh, name, &ohri);
	if (IS_ERR_VALUE(ret))
		return ret;

	return soc_ops.is_hardreset_asserted(oh, &ohri);
}

/**
 * _are_any_hardreset_lines_asserted - return true if part of @oh is hard-reset
 * @oh: struct omap_hwmod *
 *
 * If any hardreset line associated with @oh is asserted, then return true.
 * Otherwise, if @oh has no hardreset lines associated with it, or if
 * no hardreset lines associated with @oh are asserted, then return false.
 * This function is used to avoid executing some parts of the IP block
 * enable/disable sequence if a hardreset line is set.
 */
static bool _are_any_hardreset_lines_asserted(struct omap_hwmod *oh)
{
	int i;

	if (oh->rst_lines_cnt == 0)
		return false;

	for (i = 0; i < oh->rst_lines_cnt; i++)
		if (_read_hardreset(oh, oh->rst_lines[i].name) > 0)
			return true;

	return false;
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

	if (!oh->clkdm || !oh->prcm.omap4.modulemode)
		return -EINVAL;

	pr_debug("omap_hwmod: %s: %s\n", oh->name, __func__);

	omap4_cminst_module_disable(oh->clkdm->prcm_partition,
				    oh->clkdm->cm_inst,
				    oh->clkdm->clkdm_offs,
				    oh->prcm.omap4.clkctrl_offs);

	if (_are_any_hardreset_lines_asserted(oh))
		return 0;

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
	u32 v, softrst_mask;
	int c = 0;
	int ret = 0;

	if (!oh->class->sysc ||
	    !(oh->class->sysc->sysc_flags & SYSC_HAS_SOFTRESET))
		return -ENOENT;

	/* clocks must be on for this operation */
	if (oh->_state != _HWMOD_STATE_ENABLED) {
		pr_warning("omap_hwmod: %s: reset can only be entered from "
			   "enabled state\n", oh->name);
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

	if (oh->class->sysc->sysc_flags & SYSS_HAS_RESET_STATUS)
		omap_test_timeout((omap_hwmod_read(oh,
						    oh->class->sysc->syss_offs)
				   & SYSS_RESETDONE_MASK),
				  MAX_MODULE_SOFTRESET_WAIT, c);
	else if (oh->class->sysc->sysc_flags & SYSC_HAS_RESET_STATUS) {
		softrst_mask = (0x1 << oh->class->sysc->sysc_fields->srst_shift);
		omap_test_timeout(!(omap_hwmod_read(oh,
						     oh->class->sysc->sysc_offs)
				   & softrst_mask),
				  MAX_MODULE_SOFTRESET_WAIT, c);
	}

	if (c == MAX_MODULE_SOFTRESET_WAIT)
		pr_warning("omap_hwmod: %s: softreset failed (waited %d usec)\n",
			   oh->name, MAX_MODULE_SOFTRESET_WAIT);
	else
		pr_debug("omap_hwmod: %s: softreset in %d usec\n", oh->name, c);

	/*
	 * XXX add _HWMOD_STATE_WEDGED for modules that don't come back from
	 * _wait_target_ready() or _reset()
	 */

	ret = (c == MAX_MODULE_SOFTRESET_WAIT) ? -ETIMEDOUT : 0;

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
 * _reconfigure_io_chain - clear any I/O chain wakeups and reconfigure chain
 *
 * Call the appropriate PRM function to clear any logged I/O chain
 * wakeups and to reconfigure the chain.  This apparently needs to be
 * done upon every mux change.  Since hwmods can be concurrently
 * enabled and idled, hold a spinlock around the I/O chain
 * reconfiguration sequence.  No return value.
 *
 * XXX When the PRM code is moved to drivers, this function can be removed,
 * as the PRM infrastructure should abstract this.
 */
static void _reconfigure_io_chain(void)
{
	unsigned long flags;

	spin_lock_irqsave(&io_chain_lock, flags);

	if (cpu_is_omap34xx() && omap3_has_io_chain_ctrl())
		omap3xxx_prm_reconfigure_io_chain();
	else if (cpu_is_omap44xx())
		omap44xx_prm_reconfigure_io_chain();

	spin_unlock_irqrestore(&io_chain_lock, flags);
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
	int hwsup = 0;

	pr_debug("omap_hwmod: %s: enabling\n", oh->name);

	/*
	 * hwmods with HWMOD_INIT_NO_IDLE flag set are left in enabled
	 * state at init.  Now that someone is really trying to enable
	 * them, just ensure that the hwmod mux is set.
	 */
	if (oh->_int_flags & _HWMOD_SKIP_ENABLE) {
		/*
		 * If the caller has mux data populated, do the mux'ing
		 * which wouldn't have been done as part of the _enable()
		 * done during setup.
		 */
		if (oh->mux)
			omap_hwmod_mux(oh->mux, _HWMOD_STATE_ENABLED);

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
	 * If an IP block contains HW reset lines and any of them are
	 * asserted, we let integration code associated with that
	 * block handle the enable.  We've received very little
	 * information on what those driver authors need, and until
	 * detailed information is provided and the driver code is
	 * posted to the public lists, this is probably the best we
	 * can do.
	 */
	if (_are_any_hardreset_lines_asserted(oh))
		return 0;

	/* Mux pins for device runtime if populated */
	if (oh->mux && (!oh->mux->enabled ||
			((oh->_state == _HWMOD_STATE_IDLE) &&
			 oh->mux->pads_dynamic))) {
		omap_hwmod_mux(oh->mux, _HWMOD_STATE_ENABLED);
		_reconfigure_io_chain();
	}

	_add_initiator_dep(oh, mpu_oh);

	if (oh->clkdm) {
		/*
		 * A clockdomain must be in SW_SUP before enabling
		 * completely the module. The clockdomain can be set
		 * in HW_AUTO only when the module become ready.
		 */
		hwsup = clkdm_in_hwsup(oh->clkdm);
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

	r = (soc_ops.wait_target_ready) ? soc_ops.wait_target_ready(oh) :
		-EINVAL;
	if (!r) {
		/*
		 * Set the clockdomain to HW_AUTO only if the target is ready,
		 * assuming that the previous state was HW_AUTO
		 */
		if (oh->clkdm && hwsup)
			clkdm_allow_idle(oh->clkdm);

		oh->_state = _HWMOD_STATE_ENABLED;

		/* Access the sysconfig only if the target is ready */
		if (oh->class->sysc) {
			if (!(oh->_int_flags & _HWMOD_SYSCONFIG_LOADED))
				_update_sysc_cache(oh);
			_enable_sysc(oh);
		}
	} else {
		_disable_clocks(oh);
		pr_debug("omap_hwmod: %s: _wait_target_ready: %d\n",
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
	pr_debug("omap_hwmod: %s: idling\n", oh->name);

	if (oh->_state != _HWMOD_STATE_ENABLED) {
		WARN(1, "omap_hwmod: %s: idle state can only be entered from enabled state\n",
			oh->name);
		return -EINVAL;
	}

	if (_are_any_hardreset_lines_asserted(oh))
		return 0;

	if (oh->class->sysc)
		_idle_sysc(oh);
	_del_initiator_dep(oh, mpu_oh);

	if (soc_ops.disable_module)
		soc_ops.disable_module(oh);

	/*
	 * The module must be in idle mode before disabling any parents
	 * clocks. Otherwise, the parent clock might be disabled before
	 * the module transition is done, and thus will prevent the
	 * transition to complete properly.
	 */
	_disable_clocks(oh);
	if (oh->clkdm)
		clkdm_hwmod_disable(oh->clkdm, oh);

	/* Mux pins for device idle if populated */
	if (oh->mux && oh->mux->pads_dynamic) {
		omap_hwmod_mux(oh->mux, _HWMOD_STATE_IDLE);
		_reconfigure_io_chain();
	}

	oh->_state = _HWMOD_STATE_IDLE;

	return 0;
}

/**
 * omap_hwmod_set_ocp_autoidle - set the hwmod's OCP autoidle bit
 * @oh: struct omap_hwmod *
 * @autoidle: desired AUTOIDLE bitfield value (0 or 1)
 *
 * Sets the IP block's OCP autoidle bit in hardware, and updates our
 * local copy. Intended to be used by drivers that require
 * direct manipulation of the AUTOIDLE bits.
 * Returns -EINVAL if @oh is null or is not in the ENABLED state, or passes
 * along the return value from _set_module_autoidle().
 *
 * Any users of this function should be scrutinized carefully.
 */
int omap_hwmod_set_ocp_autoidle(struct omap_hwmod *oh, u8 autoidle)
{
	u32 v;
	int retval = 0;
	unsigned long flags;

	if (!oh || oh->_state != _HWMOD_STATE_ENABLED)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);

	v = oh->_sysc_cache;

	retval = _set_module_autoidle(oh, autoidle, &v);

	if (!retval)
		_write_sysconfig(v, oh);

	spin_unlock_irqrestore(&oh->_lock, flags);

	return retval;
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

	if (oh->_state != _HWMOD_STATE_IDLE &&
	    oh->_state != _HWMOD_STATE_ENABLED) {
		WARN(1, "omap_hwmod: %s: disabled state can only be entered from idle, or enabled state\n",
			oh->name);
		return -EINVAL;
	}

	if (_are_any_hardreset_lines_asserted(oh))
		return 0;

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
		if (soc_ops.disable_module)
			soc_ops.disable_module(oh);
		_disable_clocks(oh);
		if (oh->clkdm)
			clkdm_hwmod_disable(oh->clkdm, oh);
	}
	/* XXX Should this code also force-disable the optional clocks? */

	for (i = 0; i < oh->rst_lines_cnt; i++)
		_assert_hardreset(oh, oh->rst_lines[i].name);

	/* Mux pins to safe mode or use populated off mode values */
	if (oh->mux)
		omap_hwmod_mux(oh->mux, _HWMOD_STATE_DISABLED);

	oh->_state = _HWMOD_STATE_DISABLED;

	return 0;
}

/**
 * _init_mpu_rt_base - populate the virtual address for a hwmod
 * @oh: struct omap_hwmod * to locate the virtual address
 *
 * Cache the virtual address used by the MPU to access this IP block's
 * registers.  This address is needed early so the OCP registers that
 * are part of the device's address space can be ioremapped properly.
 * No return value.
 */
static void __init _init_mpu_rt_base(struct omap_hwmod *oh, void *data)
{
	struct omap_hwmod_addr_space *mem;
	void __iomem *va_start;

	if (!oh)
		return;

	_save_mpu_port_index(oh);

	if (oh->_int_flags & _HWMOD_NO_MPU_PORT)
		return;

	mem = _find_mpu_rt_addr_space(oh);
	if (!mem) {
		pr_debug("omap_hwmod: %s: no MPU register target found\n",
			 oh->name);
		return;
	}

	va_start = ioremap(mem->pa_start, mem->pa_end - mem->pa_start);
	if (!va_start) {
		pr_err("omap_hwmod: %s: Could not ioremap\n", oh->name);
		return;
	}

	pr_debug("omap_hwmod: %s: MPU register target at va %p\n",
		 oh->name, va_start);

	oh->_mpu_rt_va = va_start;
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
 * upon success or if the hwmod isn't registered, or -EINVAL upon
 * failure.
 */
static int __init _init(struct omap_hwmod *oh, void *data)
{
	int r;

	if (oh->_state != _HWMOD_STATE_REGISTERED)
		return 0;

	_init_mpu_rt_base(oh, NULL);

	r = _init_clocks(oh, NULL);
	if (IS_ERR_VALUE(r)) {
		WARN(1, "omap_hwmod: %s: couldn't init clocks\n", oh->name);
		return -EINVAL;
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
static void __init _setup_iclk_autoidle(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;
	struct list_head *p;
	int i = 0;
	if (oh->_state != _HWMOD_STATE_INITIALIZED)
		return;

	p = oh->slave_ports.next;

	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);
		if (!os->_clk)
			continue;

		if (os->flags & OCPIF_SWSUP_IDLE) {
			/* XXX omap_iclk_deny_idle(c); */
		} else {
			/* XXX omap_iclk_allow_idle(c); */
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
static int __init _setup_reset(struct omap_hwmod *oh)
{
	int r;

	if (oh->_state != _HWMOD_STATE_INITIALIZED)
		return -EINVAL;

	if (oh->rst_lines_cnt == 0) {
		r = _enable(oh);
		if (r) {
			pr_warning("omap_hwmod: %s: cannot be enabled for reset (%d)\n",
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
static void __init _setup_postsetup(struct omap_hwmod *oh)
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
	if ((oh->flags & HWMOD_INIT_NO_IDLE) &&
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
static int __init _setup(struct omap_hwmod *oh, void *data)
{
	if (oh->_state != _HWMOD_STATE_INITIALIZED)
		return 0;

	_setup_iclk_autoidle(oh);

	if (!_setup_reset(oh))
		_setup_postsetup(oh);

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
static int __init _register(struct omap_hwmod *oh)
{
	if (!oh || !oh->name || !oh->class || !oh->class->name ||
	    (oh->_state != _HWMOD_STATE_UNKNOWN))
		return -EINVAL;

	pr_debug("omap_hwmod: %s: registering\n", oh->name);

	if (_lookup(oh->name))
		return -EEXIST;

	list_add_tail(&oh->node, &omap_hwmod_list);

	INIT_LIST_HEAD(&oh->master_ports);
	INIT_LIST_HEAD(&oh->slave_ports);
	spin_lock_init(&oh->_lock);

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
 * _alloc_links - return allocated memory for hwmod links
 * @ml: pointer to a struct omap_hwmod_link * for the master link
 * @sl: pointer to a struct omap_hwmod_link * for the slave link
 *
 * Return pointers to two struct omap_hwmod_link records, via the
 * addresses pointed to by @ml and @sl.  Will first attempt to return
 * memory allocated as part of a large initial block, but if that has
 * been exhausted, will allocate memory itself.  Since ideally this
 * second allocation path will never occur, the number of these
 * 'supplemental' allocations will be logged when debugging is
 * enabled.  Returns 0.
 */
static int __init _alloc_links(struct omap_hwmod_link **ml,
			       struct omap_hwmod_link **sl)
{
	unsigned int sz;

	if ((free_ls + LINKS_PER_OCP_IF) <= max_ls) {
		*ml = &linkspace[free_ls++];
		*sl = &linkspace[free_ls++];
		return 0;
	}

	sz = sizeof(struct omap_hwmod_link) * LINKS_PER_OCP_IF;

	*sl = NULL;
	*ml = alloc_bootmem(sz);

	memset(*ml, 0, sz);

	*sl = (void *)(*ml) + sizeof(struct omap_hwmod_link);

	ls_supp++;
	pr_debug("omap_hwmod: supplemental link allocations needed: %d\n",
		 ls_supp * LINKS_PER_OCP_IF);

	return 0;
};

/**
 * _add_link - add an interconnect between two IP blocks
 * @oi: pointer to a struct omap_hwmod_ocp_if record
 *
 * Add struct omap_hwmod_link records connecting the master IP block
 * specified in @oi->master to @oi, and connecting the slave IP block
 * specified in @oi->slave to @oi.  This code is assumed to run before
 * preemption or SMP has been enabled, thus avoiding the need for
 * locking in this code.  Changes to this assumption will require
 * additional locking.  Returns 0.
 */
static int __init _add_link(struct omap_hwmod_ocp_if *oi)
{
	struct omap_hwmod_link *ml, *sl;

	pr_debug("omap_hwmod: %s -> %s: adding link\n", oi->master->name,
		 oi->slave->name);

	_alloc_links(&ml, &sl);

	ml->ocp_if = oi;
	INIT_LIST_HEAD(&ml->node);
	list_add(&ml->node, &oi->master->master_ports);
	oi->master->masters_cnt++;

	sl->ocp_if = oi;
	INIT_LIST_HEAD(&sl->node);
	list_add(&sl->node, &oi->slave->slave_ports);
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

/**
 * _alloc_linkspace - allocate large block of hwmod links
 * @ois: pointer to an array of struct omap_hwmod_ocp_if records to count
 *
 * Allocate a large block of struct omap_hwmod_link records.  This
 * improves boot time significantly by avoiding the need to allocate
 * individual records one by one.  If the number of records to
 * allocate in the block hasn't been manually specified, this function
 * will count the number of struct omap_hwmod_ocp_if records in @ois
 * and use that to determine the allocation size.  For SoC families
 * that require multiple list registrations, such as OMAP3xxx, this
 * estimation process isn't optimal, so manual estimation is advised
 * in those cases.  Returns -EEXIST if the allocation has already occurred
 * or 0 upon success.
 */
static int __init _alloc_linkspace(struct omap_hwmod_ocp_if **ois)
{
	unsigned int i = 0;
	unsigned int sz;

	if (linkspace) {
		WARN(1, "linkspace already allocated\n");
		return -EEXIST;
	}

	if (max_ls == 0)
		while (ois[i++])
			max_ls += LINKS_PER_OCP_IF;

	sz = sizeof(struct omap_hwmod_link) * max_ls;

	pr_debug("omap_hwmod: %s: allocating %d byte linkspace (%d links)\n",
		 __func__, sz, max_ls);

	linkspace = alloc_bootmem(sz);

	memset(linkspace, 0, sz);

	return 0;
}

/* Static functions intended only for use in soc_ops field function pointers */

/**
 * _omap2_wait_target_ready - wait for a module to leave slave idle
 * @oh: struct omap_hwmod *
 *
 * Wait for a module @oh to leave slave idle.  Returns 0 if the module
 * does not have an IDLEST bit or if the module successfully leaves
 * slave idle; otherwise, pass along the return value of the
 * appropriate *_cm*_wait_module_ready() function.
 */
static int _omap2_wait_target_ready(struct omap_hwmod *oh)
{
	if (!oh)
		return -EINVAL;

	if (oh->flags & HWMOD_NO_IDLEST)
		return 0;

	if (!_find_mpu_rt_port(oh))
		return 0;

	/* XXX check module SIDLEMODE, hardreset status, enabled clocks */

	return omap2_cm_wait_module_ready(oh->prcm.omap2.module_offs,
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
	if (!oh || !oh->clkdm)
		return -EINVAL;

	if (oh->flags & HWMOD_NO_IDLEST)
		return 0;

	if (!_find_mpu_rt_port(oh))
		return 0;

	/* XXX check module SIDLEMODE, hardreset status */

	return omap4_cminst_wait_module_ready(oh->clkdm->prcm_partition,
					      oh->clkdm->cm_inst,
					      oh->clkdm->clkdm_offs,
					      oh->prcm.omap4.clkctrl_offs);
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
	return omap2_prm_assert_hardreset(oh->prcm.omap2.module_offs,
					  ohri->rst_shift);
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
	return omap2_prm_deassert_hardreset(oh->prcm.omap2.module_offs,
					    ohri->rst_shift,
					    ohri->st_shift);
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
	return omap2_prm_is_hardreset_asserted(oh->prcm.omap2.module_offs,
					       ohri->st_shift);
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

	return omap4_prminst_assert_hardreset(ohri->rst_shift,
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
	return omap4_prminst_deassert_hardreset(ohri->rst_shift,
				oh->clkdm->pwrdm.ptr->prcm_partition,
				oh->clkdm->pwrdm.ptr->prcm_offs,
				oh->prcm.omap4.rstctrl_offs);
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

	return omap4_prminst_is_hardreset_asserted(ohri->rst_shift,
				oh->clkdm->pwrdm.ptr->prcm_partition,
				oh->clkdm->pwrdm.ptr->prcm_offs,
				oh->prcm.omap4.rstctrl_offs);
}

/* Public functions */

u32 omap_hwmod_read(struct omap_hwmod *oh, u16 reg_offs)
{
	if (oh->flags & HWMOD_16BIT_REG)
		return __raw_readw(oh->_mpu_rt_va + reg_offs);
	else
		return __raw_readl(oh->_mpu_rt_va + reg_offs);
}

void omap_hwmod_write(u32 v, struct omap_hwmod *oh, u16 reg_offs)
{
	if (oh->flags & HWMOD_16BIT_REG)
		__raw_writew(v, oh->_mpu_rt_va + reg_offs);
	else
		__raw_writel(v, oh->_mpu_rt_va + reg_offs);
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

error:
	return ret;
}

/**
 * omap_hwmod_set_slave_idlemode - set the hwmod's OCP slave idlemode
 * @oh: struct omap_hwmod *
 * @idlemode: SIDLEMODE field bits (shifted to bit 0)
 *
 * Sets the IP block's OCP slave idlemode in hardware, and updates our
 * local copy.  Intended to be used by drivers that have some erratum
 * that requires direct manipulation of the SIDLEMODE bits.  Returns
 * -EINVAL if @oh is null, or passes along the return value from
 * _set_slave_idlemode().
 *
 * XXX Does this function have any current users?  If not, we should
 * remove it; it is better to let the rest of the hwmod code handle this.
 * Any users of this function should be scrutinized carefully.
 */
int omap_hwmod_set_slave_idlemode(struct omap_hwmod *oh, u8 idlemode)
{
	u32 v;
	int retval = 0;

	if (!oh)
		return -EINVAL;

	v = oh->_sysc_cache;

	retval = _set_slave_idlemode(oh, idlemode, &v);
	if (!retval)
		_write_sysconfig(v, oh);

	return retval;
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

	if (!linkspace) {
		if (_alloc_linkspace(ois)) {
			pr_err("omap_hwmod: could not allocate link space\n");
			return -ENOMEM;
		}
	}

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
	_ensure_mpu_hwmod_is_setup(NULL);

	omap_hwmod_for_each(_init, NULL);
	omap_hwmod_for_each(_setup, NULL);

	return 0;
}
core_initcall(omap_hwmod_setup_all);

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
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	_idle(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return 0;
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
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	_shutdown(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return 0;
}

/**
 * omap_hwmod_enable_clocks - enable main_clk, all interface clocks
 * @oh: struct omap_hwmod *oh
 *
 * Intended to be called by the omap_device code.
 */
int omap_hwmod_enable_clocks(struct omap_hwmod *oh)
{
	unsigned long flags;

	spin_lock_irqsave(&oh->_lock, flags);
	_enable_clocks(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return 0;
}

/**
 * omap_hwmod_disable_clocks - disable main_clk, all interface clocks
 * @oh: struct omap_hwmod *oh
 *
 * Intended to be called by the omap_device code.
 */
int omap_hwmod_disable_clocks(struct omap_hwmod *oh)
{
	unsigned long flags;

	spin_lock_irqsave(&oh->_lock, flags);
	_disable_clocks(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return 0;
}

/**
 * omap_hwmod_ocp_barrier - wait for posted writes against the hwmod to complete
 * @oh: struct omap_hwmod *oh
 *
 * Intended to be called by drivers and core code when all posted
 * writes to a device must complete before continuing further
 * execution (for example, after clearing some device IRQSTATUS
 * register bits)
 *
 * XXX what about targets with multiple OCP threads?
 */
void omap_hwmod_ocp_barrier(struct omap_hwmod *oh)
{
	BUG_ON(!oh);

	if (!oh->class->sysc || !oh->class->sysc->sysc_flags) {
		WARN(1, "omap_device: %s: OCP barrier impossible due to device configuration\n",
			oh->name);
		return;
	}

	/*
	 * Forces posted writes to complete on the OCP thread handling
	 * register writes
	 */
	omap_hwmod_read(oh, oh->class->sysc->sysc_offs);
}

/**
 * omap_hwmod_reset - reset the hwmod
 * @oh: struct omap_hwmod *
 *
 * Under some conditions, a driver may wish to reset the entire device.
 * Called from omap_device code.  Returns -EINVAL on error or passes along
 * the return value from _reset().
 */
int omap_hwmod_reset(struct omap_hwmod *oh)
{
	int r;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	r = _reset(oh);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return r;
}

/*
 * IP block data retrieval functions
 */

/**
 * omap_hwmod_count_resources - count number of struct resources needed by hwmod
 * @oh: struct omap_hwmod *
 * @res: pointer to the first element of an array of struct resource to fill
 *
 * Count the number of struct resource array elements necessary to
 * contain omap_hwmod @oh resources.  Intended to be called by code
 * that registers omap_devices.  Intended to be used to determine the
 * size of a dynamically-allocated struct resource array, before
 * calling omap_hwmod_fill_resources().  Returns the number of struct
 * resource array elements needed.
 *
 * XXX This code is not optimized.  It could attempt to merge adjacent
 * resource IDs.
 *
 */
int omap_hwmod_count_resources(struct omap_hwmod *oh)
{
	struct omap_hwmod_ocp_if *os;
	struct list_head *p;
	int ret;
	int i = 0;

	ret = _count_mpu_irqs(oh) + _count_sdma_reqs(oh);

	p = oh->slave_ports.next;

	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);
		ret += _count_ocp_if_addr_spaces(os);
	}

	return ret;
}

/**
 * omap_hwmod_fill_resources - fill struct resource array with hwmod data
 * @oh: struct omap_hwmod *
 * @res: pointer to the first element of an array of struct resource to fill
 *
 * Fill the struct resource array @res with resource data from the
 * omap_hwmod @oh.  Intended to be called by code that registers
 * omap_devices.  See also omap_hwmod_count_resources().  Returns the
 * number of array elements filled.
 */
int omap_hwmod_fill_resources(struct omap_hwmod *oh, struct resource *res)
{
	struct omap_hwmod_ocp_if *os;
	struct list_head *p;
	int i, j, mpu_irqs_cnt, sdma_reqs_cnt, addr_cnt;
	int r = 0;

	/* For each IRQ, DMA, memory area, fill in array.*/

	mpu_irqs_cnt = _count_mpu_irqs(oh);
	for (i = 0; i < mpu_irqs_cnt; i++) {
		(res + r)->name = (oh->mpu_irqs + i)->name;
		(res + r)->start = (oh->mpu_irqs + i)->irq;
		(res + r)->end = (oh->mpu_irqs + i)->irq;
		(res + r)->flags = IORESOURCE_IRQ;
		r++;
	}

	sdma_reqs_cnt = _count_sdma_reqs(oh);
	for (i = 0; i < sdma_reqs_cnt; i++) {
		(res + r)->name = (oh->sdma_reqs + i)->name;
		(res + r)->start = (oh->sdma_reqs + i)->dma_req;
		(res + r)->end = (oh->sdma_reqs + i)->dma_req;
		(res + r)->flags = IORESOURCE_DMA;
		r++;
	}

	p = oh->slave_ports.next;

	i = 0;
	while (i < oh->slaves_cnt) {
		os = _fetch_next_ocp_if(&p, &i);
		addr_cnt = _count_ocp_if_addr_spaces(os);

		for (j = 0; j < addr_cnt; j++) {
			(res + r)->name = (os->addr + j)->name;
			(res + r)->start = (os->addr + j)->pa_start;
			(res + r)->end = (os->addr + j)->pa_end;
			(res + r)->flags = IORESOURCE_MEM;
			r++;
		}
	}

	return r;
}

/**
 * omap_hwmod_get_resource_byname - fetch IP block integration data by name
 * @oh: struct omap_hwmod * to operate on
 * @type: one of the IORESOURCE_* constants from include/linux/ioport.h
 * @name: pointer to the name of the data to fetch (optional)
 * @rsrc: pointer to a struct resource, allocated by the caller
 *
 * Retrieve MPU IRQ, SDMA request line, or address space start/end
 * data for the IP block pointed to by @oh.  The data will be filled
 * into a struct resource record pointed to by @rsrc.  The struct
 * resource must be allocated by the caller.  When @name is non-null,
 * the data associated with the matching entry in the IRQ/SDMA/address
 * space hwmod data arrays will be returned.  If @name is null, the
 * first array entry will be returned.  Data order is not meaningful
 * in hwmod data, so callers are strongly encouraged to use a non-null
 * @name whenever possible to avoid unpredictable effects if hwmod
 * data is later added that causes data ordering to change.  This
 * function is only intended for use by OMAP core code.  Device
 * drivers should not call this function - the appropriate bus-related
 * data accessor functions should be used instead.  Returns 0 upon
 * success or a negative error code upon error.
 */
int omap_hwmod_get_resource_byname(struct omap_hwmod *oh, unsigned int type,
				   const char *name, struct resource *rsrc)
{
	int r;
	unsigned int irq, dma;
	u32 pa_start, pa_end;

	if (!oh || !rsrc)
		return -EINVAL;

	if (type == IORESOURCE_IRQ) {
		r = _get_mpu_irq_by_name(oh, name, &irq);
		if (r)
			return r;

		rsrc->start = irq;
		rsrc->end = irq;
	} else if (type == IORESOURCE_DMA) {
		r = _get_sdma_req_by_name(oh, name, &dma);
		if (r)
			return r;

		rsrc->start = dma;
		rsrc->end = dma;
	} else if (type == IORESOURCE_MEM) {
		r = _get_addr_space_by_name(oh, name, &pa_start, &pa_end);
		if (r)
			return r;

		rsrc->start = pa_start;
		rsrc->end = pa_end;
	} else {
		return -EINVAL;
	}

	rsrc->flags = type;
	rsrc->name = name;

	return 0;
}

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

	if (!oh)
		return NULL;

	if (oh->_clk) {
		c = oh->_clk;
	} else {
		oi = _find_mpu_rt_port(oh);
		if (!oi)
			return NULL;
		c = oi->_clk;
	}

	if (!c->clkdm)
		return NULL;

	return c->clkdm->pwrdm.ptr;

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

/**
 * omap_hwmod_add_initiator_dep - add sleepdep from @init_oh to @oh
 * @oh: struct omap_hwmod *
 * @init_oh: struct omap_hwmod * (initiator)
 *
 * Add a sleep dependency between the initiator @init_oh and @oh.
 * Intended to be called by DSP/Bridge code via platform_data for the
 * DSP case; and by the DMA code in the sDMA case.  DMA code, *Bridge
 * code needs to add/del initiator dependencies dynamically
 * before/after accessing a device.  Returns the return value from
 * _add_initiator_dep().
 *
 * XXX Keep a usecount in the clockdomain code
 */
int omap_hwmod_add_initiator_dep(struct omap_hwmod *oh,
				 struct omap_hwmod *init_oh)
{
	return _add_initiator_dep(oh, init_oh);
}

/*
 * XXX what about functions for drivers to save/restore ocp_sysconfig
 * for context save/restore operations?
 */

/**
 * omap_hwmod_del_initiator_dep - remove sleepdep from @init_oh to @oh
 * @oh: struct omap_hwmod *
 * @init_oh: struct omap_hwmod * (initiator)
 *
 * Remove a sleep dependency between the initiator @init_oh and @oh.
 * Intended to be called by DSP/Bridge code via platform_data for the
 * DSP case; and by the DMA code in the sDMA case.  DMA code, *Bridge
 * code needs to add/del initiator dependencies dynamically
 * before/after accessing a device.  Returns the return value from
 * _del_initiator_dep().
 *
 * XXX Keep a usecount in the clockdomain code
 */
int omap_hwmod_del_initiator_dep(struct omap_hwmod *oh,
				 struct omap_hwmod *init_oh)
{
	return _del_initiator_dep(oh, init_oh);
}

/**
 * omap_hwmod_enable_wakeup - allow device to wake up the system
 * @oh: struct omap_hwmod *
 *
 * Sets the module OCP socket ENAWAKEUP bit to allow the module to
 * send wakeups to the PRCM, and enable I/O ring wakeup events for
 * this IP block if it has dynamic mux entries.  Eventually this
 * should set PRCM wakeup registers to cause the PRCM to receive
 * wakeup events from the module.  Does not set any wakeup routing
 * registers beyond this point - if the module is to wake up any other
 * module or subsystem, that must be set separately.  Called by
 * omap_device code.  Returns -EINVAL on error or 0 upon success.
 */
int omap_hwmod_enable_wakeup(struct omap_hwmod *oh)
{
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&oh->_lock, flags);

	if (oh->class->sysc &&
	    (oh->class->sysc->sysc_flags & SYSC_HAS_ENAWAKEUP)) {
		v = oh->_sysc_cache;
		_enable_wakeup(oh, &v);
		_write_sysconfig(v, oh);
	}

	_set_idle_ioring_wakeup(oh, true);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return 0;
}

/**
 * omap_hwmod_disable_wakeup - prevent device from waking the system
 * @oh: struct omap_hwmod *
 *
 * Clears the module OCP socket ENAWAKEUP bit to prevent the module
 * from sending wakeups to the PRCM, and disable I/O ring wakeup
 * events for this IP block if it has dynamic mux entries.  Eventually
 * this should clear PRCM wakeup registers to cause the PRCM to ignore
 * wakeup events from the module.  Does not set any wakeup routing
 * registers beyond this point - if the module is to wake up any other
 * module or subsystem, that must be set separately.  Called by
 * omap_device code.  Returns -EINVAL on error or 0 upon success.
 */
int omap_hwmod_disable_wakeup(struct omap_hwmod *oh)
{
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&oh->_lock, flags);

	if (oh->class->sysc &&
	    (oh->class->sysc->sysc_flags & SYSC_HAS_ENAWAKEUP)) {
		v = oh->_sysc_cache;
		_disable_wakeup(oh, &v);
		_write_sysconfig(v, oh);
	}

	_set_idle_ioring_wakeup(oh, false);
	spin_unlock_irqrestore(&oh->_lock, flags);

	return 0;
}

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
 * omap_hwmod_read_hardreset - read the HW reset line state of submodules
 * contained in the hwmod module
 * @oh: struct omap_hwmod *
 * @name: name of the reset line to look up and read
 *
 * Return the current state of the hwmod @oh's reset line named @name:
 * returns -EINVAL upon parameter error or if this operation
 * is unsupported on the current OMAP; otherwise, passes along the return
 * value from _read_hardreset().
 */
int omap_hwmod_read_hardreset(struct omap_hwmod *oh, const char *name)
{
	int ret;
	unsigned long flags;

	if (!oh)
		return -EINVAL;

	spin_lock_irqsave(&oh->_lock, flags);
	ret = _read_hardreset(oh, name);
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
 * Query the powerdomain of of @oh to get the context loss
 * count for this device.
 *
 * Returns the context loss count of the powerdomain assocated with @oh
 * upon success, or zero if no powerdomain exists for @oh.
 */
int omap_hwmod_get_context_loss_count(struct omap_hwmod *oh)
{
	struct powerdomain *pwrdm;
	int ret = 0;

	pwrdm = omap_hwmod_get_pwrdm(oh);
	if (pwrdm)
		ret = pwrdm_get_context_loss_count(pwrdm);

	return ret;
}

/**
 * omap_hwmod_no_setup_reset - prevent a hwmod from being reset upon setup
 * @oh: struct omap_hwmod *
 *
 * Prevent the hwmod @oh from being reset during the setup process.
 * Intended for use by board-*.c files on boards with devices that
 * cannot tolerate being reset.  Must be called before the hwmod has
 * been set up.  Returns 0 upon success or negative error code upon
 * failure.
 */
int omap_hwmod_no_setup_reset(struct omap_hwmod *oh)
{
	if (!oh)
		return -EINVAL;

	if (oh->_state != _HWMOD_STATE_REGISTERED) {
		pr_err("omap_hwmod: %s: cannot prevent setup reset; in wrong state\n",
			oh->name);
		return -EINVAL;
	}

	oh->flags |= HWMOD_INIT_NO_RESET;

	return 0;
}

/**
 * omap_hwmod_pad_route_irq - route an I/O pad wakeup to a particular MPU IRQ
 * @oh: struct omap_hwmod * containing hwmod mux entries
 * @pad_idx: array index in oh->mux of the hwmod mux entry to route wakeup
 * @irq_idx: the hwmod mpu_irqs array index of the IRQ to trigger on wakeup
 *
 * When an I/O pad wakeup arrives for the dynamic or wakeup hwmod mux
 * entry number @pad_idx for the hwmod @oh, trigger the interrupt
 * service routine for the hwmod's mpu_irqs array index @irq_idx.  If
 * this function is not called for a given pad_idx, then the ISR
 * associated with @oh's first MPU IRQ will be triggered when an I/O
 * pad wakeup occurs on that pad.  Note that @pad_idx is the index of
 * the _dynamic or wakeup_ entry: if there are other entries not
 * marked with OMAP_DEVICE_PAD_WAKEUP or OMAP_DEVICE_PAD_REMUX, these
 * entries are NOT COUNTED in the dynamic pad index.  This function
 * must be called separately for each pad that requires its interrupt
 * to be re-routed this way.  Returns -EINVAL if there is an argument
 * problem or if @oh does not have hwmod mux entries or MPU IRQs;
 * returns -ENOMEM if memory cannot be allocated; or 0 upon success.
 *
 * XXX This function interface is fragile.  Rather than using array
 * indexes, which are subject to unpredictable change, it should be
 * using hwmod IRQ names, and some other stable key for the hwmod mux
 * pad records.
 */
int omap_hwmod_pad_route_irq(struct omap_hwmod *oh, int pad_idx, int irq_idx)
{
	int nr_irqs;

	might_sleep();

	if (!oh || !oh->mux || !oh->mpu_irqs || pad_idx < 0 ||
	    pad_idx >= oh->mux->nr_pads_dynamic)
		return -EINVAL;

	/* Check the number of available mpu_irqs */
	for (nr_irqs = 0; oh->mpu_irqs[nr_irqs].irq >= 0; nr_irqs++)
		;

	if (irq_idx >= nr_irqs)
		return -EINVAL;

	if (!oh->mux->irqs) {
		/* XXX What frees this? */
		oh->mux->irqs = kzalloc(sizeof(int) * oh->mux->nr_pads_dynamic,
			GFP_KERNEL);
		if (!oh->mux->irqs)
			return -ENOMEM;
	}
	oh->mux->irqs[pad_idx] = irq_idx;

	return 0;
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
	if (cpu_is_omap24xx() || cpu_is_omap34xx()) {
		soc_ops.wait_target_ready = _omap2_wait_target_ready;
		soc_ops.assert_hardreset = _omap2_assert_hardreset;
		soc_ops.deassert_hardreset = _omap2_deassert_hardreset;
		soc_ops.is_hardreset_asserted = _omap2_is_hardreset_asserted;
	} else if (cpu_is_omap44xx() || soc_is_omap54xx()) {
		soc_ops.enable_module = _omap4_enable_module;
		soc_ops.disable_module = _omap4_disable_module;
		soc_ops.wait_target_ready = _omap4_wait_target_ready;
		soc_ops.assert_hardreset = _omap4_assert_hardreset;
		soc_ops.deassert_hardreset = _omap4_deassert_hardreset;
		soc_ops.is_hardreset_asserted = _omap4_is_hardreset_asserted;
		soc_ops.init_clkdm = _init_clkdm;
	} else {
		WARN(1, "omap_hwmod: unknown SoC type\n");
	}

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
