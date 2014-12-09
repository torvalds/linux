/*
 * OMAP2+ Clock Management prototypes
 *
 * Copyright (C) 2007-2009, 2012 Texas Instruments, Inc.
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ASM_MACH_OMAP2_CM_H
#define __ARCH_ASM_MACH_OMAP2_CM_H

/*
 * MAX_MODULE_READY_TIME: max duration in microseconds to wait for the
 * PRCM to request that a module exit the inactive state in the case of
 * OMAP2 & 3.
 * In the case of OMAP4 this is the max duration in microseconds for the
 * module to reach the functionnal state from an inactive state.
 */
#define MAX_MODULE_READY_TIME		2000

# ifndef __ASSEMBLER__
extern void __iomem *cm_base;
extern void __iomem *cm2_base;
extern void omap2_set_globals_cm(void __iomem *cm, void __iomem *cm2);
# endif

/*
 * MAX_MODULE_DISABLE_TIME: max duration in microseconds to wait for
 * the PRCM to request that a module enter the inactive state in the
 * case of OMAP2 & 3.  In the case of OMAP4 this is the max duration
 * in microseconds for the module to reach the inactive state from
 * a functional state.
 * XXX FSUSB on OMAP4430 takes ~4ms to idle after reset during
 * kernel init.
 */
#define MAX_MODULE_DISABLE_TIME		5000

# ifndef __ASSEMBLER__

/**
 * struct cm_ll_data - fn ptrs to per-SoC CM function implementations
 * @split_idlest_reg: ptr to the SoC CM-specific split_idlest_reg impl
 * @wait_module_ready: ptr to the SoC CM-specific wait_module_ready impl
 * @wait_module_idle: ptr to the SoC CM-specific wait_module_idle impl
 * @module_enable: ptr to the SoC CM-specific module_enable impl
 * @module_disable: ptr to the SoC CM-specific module_disable impl
 */
struct cm_ll_data {
	int (*split_idlest_reg)(void __iomem *idlest_reg, s16 *prcm_inst,
				u8 *idlest_reg_id);
	int (*wait_module_ready)(u8 part, s16 prcm_mod, u16 idlest_reg,
				 u8 idlest_shift);
	int (*wait_module_idle)(u8 part, s16 prcm_mod, u16 idlest_reg,
				u8 idlest_shift);
	void (*module_enable)(u8 mode, u8 part, u16 inst, u16 clkctrl_offs);
	void (*module_disable)(u8 part, u16 inst, u16 clkctrl_offs);
};

extern int cm_split_idlest_reg(void __iomem *idlest_reg, s16 *prcm_inst,
			       u8 *idlest_reg_id);
int omap_cm_wait_module_ready(u8 part, s16 prcm_mod, u16 idlest_reg,
			      u8 idlest_shift);
int omap_cm_wait_module_idle(u8 part, s16 prcm_mod, u16 idlest_reg,
			     u8 idlest_shift);
int omap_cm_module_enable(u8 mode, u8 part, u16 inst, u16 clkctrl_offs);
int omap_cm_module_disable(u8 part, u16 inst, u16 clkctrl_offs);
extern int cm_register(struct cm_ll_data *cld);
extern int cm_unregister(struct cm_ll_data *cld);

# endif

#endif
