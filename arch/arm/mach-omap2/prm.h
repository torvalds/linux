/*
 * OMAP2/3/4 Power/Reset Management (PRM) bitfield definitions
 *
 * Copyright (C) 2007-2009, 2012 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 *
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ARM_MACH_OMAP2_PRM_H
#define __ARCH_ARM_MACH_OMAP2_PRM_H

#include "prcm-common.h"

# ifndef __ASSEMBLER__
extern void __iomem *prm_base;
extern u16 prm_features;
extern void omap2_set_globals_prm(void __iomem *prm);
int omap_prcm_init(void);
int omap2_prm_base_init(void);
int omap2_prcm_base_init(void);
# endif

/*
 * prm_features flag values
 *
 * PRM_HAS_IO_WAKEUP: has IO wakeup capability
 * PRM_HAS_VOLTAGE: has voltage domains
 * PRM_IRQ_DEFAULT: use default irq number for PRM irq
 */
#define PRM_HAS_IO_WAKEUP	BIT(0)
#define PRM_HAS_VOLTAGE		BIT(1)
#define PRM_IRQ_DEFAULT		BIT(2)

/*
 * MAX_MODULE_SOFTRESET_WAIT: Maximum microseconds to wait for OMAP
 * module to softreset
 */
#define MAX_MODULE_SOFTRESET_WAIT		10000

/*
 * MAX_MODULE_HARDRESET_WAIT: Maximum microseconds to wait for an OMAP
 * submodule to exit hardreset
 */
#define MAX_MODULE_HARDRESET_WAIT		10000

/*
 * Register bitfields
 */

/*
 * 24XX: PM_PWSTST_CORE, PM_PWSTST_GFX, PM_PWSTST_MPU, PM_PWSTST_DSP
 *
 * 2430: PM_PWSTST_MDM
 *
 * 3430: PM_PWSTST_IVA2, PM_PWSTST_MPU, PM_PWSTST_CORE, PM_PWSTST_GFX,
 *	 PM_PWSTST_DSS, PM_PWSTST_CAM, PM_PWSTST_PER, PM_PWSTST_EMU,
 *	 PM_PWSTST_NEON
 */
#define OMAP_INTRANSITION_MASK				(1 << 20)


/*
 * 24XX: PM_PWSTST_GFX, PM_PWSTST_DSP
 *
 * 2430: PM_PWSTST_MDM
 *
 * 3430: PM_PWSTST_IVA2, PM_PWSTST_MPU, PM_PWSTST_CORE, PM_PWSTST_GFX,
 *	 PM_PWSTST_DSS, PM_PWSTST_CAM, PM_PWSTST_PER, PM_PWSTST_EMU,
 *	 PM_PWSTST_NEON
 */
#define OMAP_POWERSTATEST_SHIFT				0
#define OMAP_POWERSTATEST_MASK				(0x3 << 0)

/*
 * 24XX: PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE, PM_PWSTCTRL_GFX,
 *       PM_PWSTCTRL_DSP, PM_PWSTST_MPU
 *
 * 2430: PM_PWSTCTRL_MDM shared bits
 *
 * 3430: PM_PWSTCTRL_IVA2, PM_PWSTCTRL_MPU, PM_PWSTCTRL_CORE,
 *	 PM_PWSTCTRL_GFX, PM_PWSTCTRL_DSS, PM_PWSTCTRL_CAM, PM_PWSTCTRL_PER,
 *	 PM_PWSTCTRL_NEON shared bits
 */
#define OMAP_POWERSTATE_SHIFT				0
#define OMAP_POWERSTATE_MASK				(0x3 << 0)

/*
 * Standardized OMAP reset source bits
 *
 * To the extent these happen to match the hardware register bit
 * shifts, it's purely coincidental.  Used by omap-wdt.c.
 * OMAP_UNKNOWN_RST_SRC_ID_SHIFT is a special value, used whenever
 * there are any bits remaining in the global PRM_RSTST register that
 * haven't been identified, or when the PRM code for the current SoC
 * doesn't know how to interpret the register.
 */
#define OMAP_GLOBAL_COLD_RST_SRC_ID_SHIFT			0
#define OMAP_GLOBAL_WARM_RST_SRC_ID_SHIFT			1
#define OMAP_SECU_VIOL_RST_SRC_ID_SHIFT				2
#define OMAP_MPU_WD_RST_SRC_ID_SHIFT				3
#define OMAP_SECU_WD_RST_SRC_ID_SHIFT				4
#define OMAP_EXTWARM_RST_SRC_ID_SHIFT				5
#define OMAP_VDD_MPU_VM_RST_SRC_ID_SHIFT			6
#define OMAP_VDD_IVA_VM_RST_SRC_ID_SHIFT			7
#define OMAP_VDD_CORE_VM_RST_SRC_ID_SHIFT			8
#define OMAP_ICEPICK_RST_SRC_ID_SHIFT				9
#define OMAP_ICECRUSHER_RST_SRC_ID_SHIFT			10
#define OMAP_C2C_RST_SRC_ID_SHIFT				11
#define OMAP_UNKNOWN_RST_SRC_ID_SHIFT				12

#ifndef __ASSEMBLER__

/**
 * struct prm_reset_src_map - map register bitshifts to standard bitshifts
 * @reg_shift: bitshift in the PRM reset source register
 * @std_shift: bitshift equivalent in the standard reset source list
 *
 * The fields are signed because -1 is used as a terminator.
 */
struct prm_reset_src_map {
	s8 reg_shift;
	s8 std_shift;
};

/**
 * struct prm_ll_data - fn ptrs to per-SoC PRM function implementations
 * @read_reset_sources: ptr to the SoC PRM-specific get_reset_source impl
 * @was_any_context_lost_old: ptr to the SoC PRM context loss test fn
 * @clear_context_loss_flags_old: ptr to the SoC PRM context loss flag clear fn
 * @late_init: ptr to the late init function
 * @assert_hardreset: ptr to the SoC PRM hardreset assert impl
 * @deassert_hardreset: ptr to the SoC PRM hardreset deassert impl
 *
 * XXX @was_any_context_lost_old and @clear_context_loss_flags_old are
 * deprecated.
 */
struct prm_ll_data {
	u32 (*read_reset_sources)(void);
	bool (*was_any_context_lost_old)(u8 part, s16 inst, u16 idx);
	void (*clear_context_loss_flags_old)(u8 part, s16 inst, u16 idx);
	int (*late_init)(void);
	int (*assert_hardreset)(u8 shift, u8 part, s16 prm_mod, u16 offset);
	int (*deassert_hardreset)(u8 shift, u8 st_shift, u8 part, s16 prm_mod,
				  u16 offset, u16 st_offset);
	int (*is_hardreset_asserted)(u8 shift, u8 part, s16 prm_mod,
				     u16 offset);
	void (*reset_system)(void);
	int (*clear_mod_irqs)(s16 module, u8 regs, u32 wkst_mask);
	u32 (*vp_check_txdone)(u8 vp_id);
	void (*vp_clear_txdone)(u8 vp_id);
};

extern int prm_register(struct prm_ll_data *pld);
extern int prm_unregister(struct prm_ll_data *pld);

int omap_prm_assert_hardreset(u8 shift, u8 part, s16 prm_mod, u16 offset);
int omap_prm_deassert_hardreset(u8 shift, u8 st_shift, u8 part, s16 prm_mod,
				u16 offset, u16 st_offset);
int omap_prm_is_hardreset_asserted(u8 shift, u8 part, s16 prm_mod, u16 offset);
extern u32 prm_read_reset_sources(void);
extern bool prm_was_any_context_lost_old(u8 part, s16 inst, u16 idx);
extern void prm_clear_context_loss_flags_old(u8 part, s16 inst, u16 idx);
void omap_prm_reset_system(void);

void omap_prm_reconfigure_io_chain(void);
int omap_prm_clear_mod_irqs(s16 module, u8 regs, u32 wkst_mask);

/*
 * Voltage Processor (VP) identifiers
 */
#define OMAP3_VP_VDD_MPU_ID	0
#define OMAP3_VP_VDD_CORE_ID	1
#define OMAP4_VP_VDD_CORE_ID	0
#define OMAP4_VP_VDD_IVA_ID	1
#define OMAP4_VP_VDD_MPU_ID	2

u32 omap_prm_vp_check_txdone(u8 vp_id);
void omap_prm_vp_clear_txdone(u8 vp_id);

#endif


#endif
