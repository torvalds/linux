/*
 * OMAP2/3 clockdomain framework functions
 *
 * Copyright (C) 2008, 2012 Texas Instruments, Inc.
 * Copyright (C) 2008-2011 Nokia Corporation
 *
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCKDOMAIN_H
#define __ARCH_ARM_MACH_OMAP2_CLOCKDOMAIN_H

#include <linux/init.h>

#include "powerdomain.h"
#include "clock.h"

/*
 * Clockdomain flags
 *
 * XXX Document CLKDM_CAN_* flags
 *
 * CLKDM_NO_AUTODEPS: Prevent "autodeps" from being added/removed from this
 *     clockdomain.  (Currently, this applies to OMAP3 clockdomains only.)
 * CLKDM_ACTIVE_WITH_MPU: The PRCM guarantees that this clockdomain is
 *     active whenever the MPU is active.  True for interconnects and
 *     the WKUP clockdomains.
 * CLKDM_MISSING_IDLE_REPORTING: The idle status of the IP blocks and
 *     clocks inside this clockdomain are not taken into account by
 *     the PRCM when determining whether the clockdomain is idle.
 *     Without this flag, if the clockdomain is set to
 *     hardware-supervised idle mode, the PRCM may transition the
 *     enclosing powerdomain to a low power state, even when devices
 *     inside the clockdomain and powerdomain are in use.  (An example
 *     of such a clockdomain is the EMU clockdomain on OMAP3/4.)  If
 *     this flag is set, and the clockdomain does not support the
 *     force-sleep mode, then the HW_AUTO mode will be used to put the
 *     clockdomain to sleep.  Similarly, if the clockdomain supports
 *     the force-wakeup mode, then it will be used whenever a clock or
 *     IP block inside the clockdomain is active, rather than the
 *     HW_AUTO mode.
 */
#define CLKDM_CAN_FORCE_SLEEP			(1 << 0)
#define CLKDM_CAN_FORCE_WAKEUP			(1 << 1)
#define CLKDM_CAN_ENABLE_AUTO			(1 << 2)
#define CLKDM_CAN_DISABLE_AUTO			(1 << 3)
#define CLKDM_NO_AUTODEPS			(1 << 4)
#define CLKDM_ACTIVE_WITH_MPU			(1 << 5)
#define CLKDM_MISSING_IDLE_REPORTING		(1 << 6)

#define CLKDM_CAN_HWSUP		(CLKDM_CAN_ENABLE_AUTO | CLKDM_CAN_DISABLE_AUTO)
#define CLKDM_CAN_SWSUP		(CLKDM_CAN_FORCE_SLEEP | CLKDM_CAN_FORCE_WAKEUP)
#define CLKDM_CAN_HWSUP_SWSUP	(CLKDM_CAN_SWSUP | CLKDM_CAN_HWSUP)

/**
 * struct clkdm_autodep - clkdm deps to add when entering/exiting hwsup mode
 * @clkdm: clockdomain to add wkdep+sleepdep on - set name member only
 *
 * A clockdomain that should have wkdeps and sleepdeps added when a
 * clockdomain should stay active in hwsup mode; and conversely,
 * removed when the clockdomain should be allowed to go inactive in
 * hwsup mode.
 *
 * Autodeps are deprecated and should be removed after
 * omap_hwmod-based fine-grained module idle control is added.
 */
struct clkdm_autodep {
	union {
		const char *name;
		struct clockdomain *ptr;
	} clkdm;
};

/**
 * struct clkdm_dep - encode dependencies between clockdomains
 * @clkdm_name: clockdomain name
 * @clkdm: pointer to the struct clockdomain of @clkdm_name
 * @wkdep_usecount: Number of wakeup dependencies causing this clkdm to wake
 * @sleepdep_usecount: Number of sleep deps that could prevent clkdm from idle
 *
 * Statically defined.  @clkdm is resolved from @clkdm_name at runtime and
 * should not be pre-initialized.
 *
 * XXX Should also include hardware (fixed) dependencies.
 */
struct clkdm_dep {
	const char *clkdm_name;
	struct clockdomain *clkdm;
	s16 wkdep_usecount;
	s16 sleepdep_usecount;
};

/* Possible flags for struct clockdomain._flags */
#define _CLKDM_FLAG_HWSUP_ENABLED		BIT(0)

struct omap_hwmod;

/**
 * struct clockdomain - OMAP clockdomain
 * @name: clockdomain name
 * @pwrdm: powerdomain containing this clockdomain
 * @clktrctrl_reg: CLKSTCTRL reg for the given clock domain
 * @clktrctrl_mask: CLKTRCTRL/AUTOSTATE field mask in CM_CLKSTCTRL reg
 * @flags: Clockdomain capability flags
 * @_flags: Flags for use only by internal clockdomain code
 * @dep_bit: Bit shift of this clockdomain's PM_WKDEP/CM_SLEEPDEP bit
 * @prcm_partition: (OMAP4 only) PRCM partition ID for this clkdm's registers
 * @cm_inst: (OMAP4 only) CM instance register offset
 * @clkdm_offs: (OMAP4 only) CM clockdomain register offset
 * @wkdep_srcs: Clockdomains that can be told to wake this powerdomain up
 * @sleepdep_srcs: Clockdomains that can be told to keep this clkdm from inact
 * @usecount: Usecount tracking
 * @node: list_head to link all clockdomains together
 *
 * @prcm_partition should be a macro from mach-omap2/prcm44xx.h (OMAP4 only)
 * @cm_inst should be a macro ending in _INST from the OMAP4 CM instance
 *     definitions (OMAP4 only)
 * @clkdm_offs should be a macro ending in _CDOFFS from the OMAP4 CM instance
 *     definitions (OMAP4 only)
 */
struct clockdomain {
	const char *name;
	union {
		const char *name;
		struct powerdomain *ptr;
	} pwrdm;
	const u16 clktrctrl_mask;
	const u8 flags;
	u8 _flags;
	const u8 dep_bit;
	const u8 prcm_partition;
	const u16 cm_inst;
	const u16 clkdm_offs;
	struct clkdm_dep *wkdep_srcs;
	struct clkdm_dep *sleepdep_srcs;
	int usecount;
	struct list_head node;
};

/**
 * struct clkdm_ops - Arch specific function implementations
 * @clkdm_add_wkdep: Add a wakeup dependency between clk domains
 * @clkdm_del_wkdep: Delete a wakeup dependency between clk domains
 * @clkdm_read_wkdep: Read wakeup dependency state between clk domains
 * @clkdm_clear_all_wkdeps: Remove all wakeup dependencies from the clk domain
 * @clkdm_add_sleepdep: Add a sleep dependency between clk domains
 * @clkdm_del_sleepdep: Delete a sleep dependency between clk domains
 * @clkdm_read_sleepdep: Read sleep dependency state between clk domains
 * @clkdm_clear_all_sleepdeps: Remove all sleep dependencies from the clk domain
 * @clkdm_sleep: Force a clockdomain to sleep
 * @clkdm_wakeup: Force a clockdomain to wakeup
 * @clkdm_allow_idle: Enable hw supervised idle transitions for clock domain
 * @clkdm_deny_idle: Disable hw supervised idle transitions for clock domain
 * @clkdm_clk_enable: Put the clkdm in right state for a clock enable
 * @clkdm_clk_disable: Put the clkdm in right state for a clock disable
 */
struct clkdm_ops {
	int	(*clkdm_add_wkdep)(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
	int	(*clkdm_del_wkdep)(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
	int	(*clkdm_read_wkdep)(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
	int	(*clkdm_clear_all_wkdeps)(struct clockdomain *clkdm);
	int	(*clkdm_add_sleepdep)(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
	int	(*clkdm_del_sleepdep)(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
	int	(*clkdm_read_sleepdep)(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
	int	(*clkdm_clear_all_sleepdeps)(struct clockdomain *clkdm);
	int	(*clkdm_sleep)(struct clockdomain *clkdm);
	int	(*clkdm_wakeup)(struct clockdomain *clkdm);
	void	(*clkdm_allow_idle)(struct clockdomain *clkdm);
	void	(*clkdm_deny_idle)(struct clockdomain *clkdm);
	int	(*clkdm_clk_enable)(struct clockdomain *clkdm);
	int	(*clkdm_clk_disable)(struct clockdomain *clkdm);
};

int clkdm_register_platform_funcs(struct clkdm_ops *co);
int clkdm_register_autodeps(struct clkdm_autodep *ia);
int clkdm_register_clkdms(struct clockdomain **c);
int clkdm_complete_init(void);

struct clockdomain *clkdm_lookup(const char *name);

int clkdm_for_each(int (*fn)(struct clockdomain *clkdm, void *user),
			void *user);
struct powerdomain *clkdm_get_pwrdm(struct clockdomain *clkdm);

int clkdm_add_wkdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
int clkdm_del_wkdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
int clkdm_read_wkdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
int clkdm_clear_all_wkdeps(struct clockdomain *clkdm);
int clkdm_add_sleepdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
int clkdm_del_sleepdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
int clkdm_read_sleepdep(struct clockdomain *clkdm1, struct clockdomain *clkdm2);
int clkdm_clear_all_sleepdeps(struct clockdomain *clkdm);

void clkdm_allow_idle_nolock(struct clockdomain *clkdm);
void clkdm_allow_idle(struct clockdomain *clkdm);
void clkdm_deny_idle_nolock(struct clockdomain *clkdm);
void clkdm_deny_idle(struct clockdomain *clkdm);
bool clkdm_in_hwsup(struct clockdomain *clkdm);
bool clkdm_missing_idle_reporting(struct clockdomain *clkdm);

int clkdm_wakeup_nolock(struct clockdomain *clkdm);
int clkdm_wakeup(struct clockdomain *clkdm);
int clkdm_sleep_nolock(struct clockdomain *clkdm);
int clkdm_sleep(struct clockdomain *clkdm);

int clkdm_clk_enable(struct clockdomain *clkdm, struct clk *clk);
int clkdm_clk_disable(struct clockdomain *clkdm, struct clk *clk);
int clkdm_hwmod_enable(struct clockdomain *clkdm, struct omap_hwmod *oh);
int clkdm_hwmod_disable(struct clockdomain *clkdm, struct omap_hwmod *oh);

extern void __init omap242x_clockdomains_init(void);
extern void __init omap243x_clockdomains_init(void);
extern void __init omap3xxx_clockdomains_init(void);
extern void __init am33xx_clockdomains_init(void);
extern void __init ti814x_clockdomains_init(void);
extern void __init ti816x_clockdomains_init(void);
extern void __init omap44xx_clockdomains_init(void);
extern void __init omap54xx_clockdomains_init(void);
extern void __init dra7xx_clockdomains_init(void);
void am43xx_clockdomains_init(void);

extern void clkdm_add_autodeps(struct clockdomain *clkdm);
extern void clkdm_del_autodeps(struct clockdomain *clkdm);

extern struct clkdm_ops omap2_clkdm_operations;
extern struct clkdm_ops omap3_clkdm_operations;
extern struct clkdm_ops omap4_clkdm_operations;
extern struct clkdm_ops am33xx_clkdm_operations;
extern struct clkdm_ops am43xx_clkdm_operations;

extern struct clkdm_dep gfx_24xx_wkdeps[];
extern struct clkdm_dep dsp_24xx_wkdeps[];
extern struct clockdomain wkup_common_clkdm;

#endif
