/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Interface for functions that need to be run in internal SRAM
 */

#ifndef __ASSEMBLY__
#include <plat/sram.h>

extern void omap2_sram_ddr_init(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
				u32 base_cs, u32 force_unlock);
extern void omap2_sram_reprogram_sdrc(u32 perf_level, u32 dll_val,
				      u32 mem_type);
extern u32 omap2_set_prcm(u32 dpll_ctrl_val, u32 sdrc_rfr_val, int bypass);

extern void omap3_sram_restore_context(void);

/* Do not use these */
extern void omap24xx_sram_reprogram_clock(u32 ckctl, u32 dpllctl);
extern unsigned long omap24xx_sram_reprogram_clock_sz;

extern void omap242x_sram_ddr_init(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
						u32 base_cs, u32 force_unlock);
extern unsigned long omap242x_sram_ddr_init_sz;

extern u32 omap242x_sram_set_prcm(u32 dpll_ctrl_val, u32 sdrc_rfr_val,
						int bypass);
extern unsigned long omap242x_sram_set_prcm_sz;

extern void omap242x_sram_reprogram_sdrc(u32 perf_level, u32 dll_val,
						u32 mem_type);
extern unsigned long omap242x_sram_reprogram_sdrc_sz;


extern void omap243x_sram_ddr_init(u32 *slow_dll_ctrl, u32 fast_dll_ctrl,
						u32 base_cs, u32 force_unlock);
extern unsigned long omap243x_sram_ddr_init_sz;

extern u32 omap243x_sram_set_prcm(u32 dpll_ctrl_val, u32 sdrc_rfr_val,
						int bypass);
extern unsigned long omap243x_sram_set_prcm_sz;

extern void omap243x_sram_reprogram_sdrc(u32 perf_level, u32 dll_val,
						u32 mem_type);
extern unsigned long omap243x_sram_reprogram_sdrc_sz;

#ifdef CONFIG_PM
extern void omap_push_sram_idle(void);
#else
static inline void omap_push_sram_idle(void) {}
#endif /* CONFIG_PM */

#endif /* __ASSEMBLY__ */

/*
 * OMAP2+: define the SRAM PA addresses.
 * Used by the SRAM management code and the idle sleep code.
 */
#define OMAP2_SRAM_PA		0x40200000
#define OMAP3_SRAM_PA           0x40200000
