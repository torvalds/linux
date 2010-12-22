/*
 * OMAP4 Clock Management (CM) function prototypes
 *
 * Copyright (C) 2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ASM_MACH_OMAP2_CMINST44XX_H
#define __ARCH_ASM_MACH_OMAP2_CMINST44XX_H

extern bool omap4_cminst_is_clkdm_in_hwsup(u8 part, s16 inst, u16 cdoffs);
extern void omap4_cminst_clkdm_enable_hwsup(u8 part, s16 inst, u16 cdoffs);
extern void omap4_cminst_clkdm_disable_hwsup(u8 part, s16 inst, u16 cdoffs);
extern void omap4_cminst_clkdm_force_sleep(u8 part, s16 inst, u16 cdoffs);
extern void omap4_cminst_clkdm_force_wakeup(u8 part, s16 inst, u16 cdoffs);

/*
 * In an ideal world, we would not export these low-level functions,
 * but this will probably take some time to fix properly
 */
extern u32 omap4_cminst_read_inst_reg(u8 part, s16 inst, u16 idx);
extern void omap4_cminst_write_inst_reg(u32 val, u8 part, s16 inst, u16 idx);
extern u32 omap4_cminst_rmw_inst_reg_bits(u32 mask, u32 bits, u8 part,
					   s16 inst, s16 idx);

extern int omap4_cm_wait_module_ready(void __iomem *clkctrl_reg);

#endif
