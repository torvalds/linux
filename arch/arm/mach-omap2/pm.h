/*
 * OMAP2/3 Power Management Routines
 *
 * Copyright (C) 2008 Nokia Corporation
 * Jouni Hogander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ARM_MACH_OMAP2_PM_H
#define __ARCH_ARM_MACH_OMAP2_PM_H

#include <linux/err.h>

#include "powerdomain.h"

extern void *omap3_secure_ram_storage;
extern void omap3_pm_off_mode_enable(int);
extern void omap_sram_idle(void);
extern int omap3_can_sleep(void);
extern int omap_set_pwrdm_state(struct powerdomain *pwrdm, u32 state);
extern int omap3_idle_init(void);

#if defined(CONFIG_PM_OPP)
extern int omap3_opp_init(void);
extern int omap4_opp_init(void);
#else
static inline int omap3_opp_init(void)
{
	return -EINVAL;
}
static inline int omap4_opp_init(void)
{
	return -EINVAL;
}
#endif

/*
 * cpuidle mach specific parameters
 *
 * The board code can override the default C-states definition using
 * omap3_pm_init_cpuidle
 */
struct cpuidle_params {
	u32 exit_latency;	/* exit_latency = sleep + wake-up latencies */
	u32 target_residency;
	u8 valid;		/* validates the C-state */
};

#if defined(CONFIG_PM) && defined(CONFIG_CPU_IDLE)
extern void omap3_pm_init_cpuidle(struct cpuidle_params *cpuidle_board_params);
#else
static
inline void omap3_pm_init_cpuidle(struct cpuidle_params *cpuidle_board_params)
{
}
#endif

extern int omap3_pm_get_suspend_state(struct powerdomain *pwrdm);
extern int omap3_pm_set_suspend_state(struct powerdomain *pwrdm, int state);

#ifdef CONFIG_PM_DEBUG
extern u32 enable_off_mode;
#else
#define enable_off_mode 0
#endif

#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
extern void pm_dbg_update_time(struct powerdomain *pwrdm, int prev);
#else
#define pm_dbg_update_time(pwrdm, prev) do {} while (0);
#endif /* CONFIG_PM_DEBUG */

/* 24xx */
extern void omap24xx_idle_loop_suspend(void);
extern unsigned int omap24xx_idle_loop_suspend_sz;

extern void omap24xx_cpu_suspend(u32 dll_ctrl, void __iomem *sdrc_dlla_ctrl,
					void __iomem *sdrc_power);
extern unsigned int omap24xx_cpu_suspend_sz;

/* 3xxx */
extern void omap34xx_cpu_suspend(int save_state);

/* omap3_do_wfi function pointer and size, for copy to SRAM */
extern void omap3_do_wfi(void);
extern unsigned int omap3_do_wfi_sz;
/* ... and its pointer from SRAM after copy */
extern void (*omap3_do_wfi_sram)(void);

/* save_secure_ram_context function pointer and size, for copy to SRAM */
extern int save_secure_ram_context(u32 *addr);
extern unsigned int save_secure_ram_context_sz;

extern void omap3_save_scratchpad_contents(void);

#define PM_RTA_ERRATUM_i608		(1 << 0)
#define PM_SDRC_WAKEUP_ERRATUM_i583	(1 << 1)

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_OMAP3)
extern u16 pm34xx_errata;
#define IS_PM34XX_ERRATUM(id)		(pm34xx_errata & (id))
extern void enable_omap3630_toggle_l2_on_restore(void);
#else
#define IS_PM34XX_ERRATUM(id)		0
static inline void enable_omap3630_toggle_l2_on_restore(void) { }
#endif		/* defined(CONFIG_PM) && defined(CONFIG_ARCH_OMAP3) */

#ifdef CONFIG_OMAP_SMARTREFLEX
extern int omap_devinit_smartreflex(void);
extern void omap_enable_smartreflex_on_init(void);
#else
static inline int omap_devinit_smartreflex(void)
{
	return -EINVAL;
}

static inline void omap_enable_smartreflex_on_init(void) {}
#endif

#ifdef CONFIG_TWL4030_CORE
extern int omap3_twl_init(void);
extern int omap4_twl_init(void);
extern int omap3_twl_set_sr_bit(bool enable);
#else
static inline int omap3_twl_init(void)
{
	return -EINVAL;
}
static inline int omap4_twl_init(void)
{
	return -EINVAL;
}
#endif

#endif
