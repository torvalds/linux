/*
 * OMAP4 Power/Reset Management (PRM) function prototypes
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ASM_MACH_OMAP2_PRMINST44XX_H
#define __ARCH_ASM_MACH_OMAP2_PRMINST44XX_H

#define PRM_INSTANCE_UNKNOWN	-1
extern s32 omap4_prmst_get_prm_dev_inst(void);
void omap4_prminst_set_prm_dev_inst(s32 dev_inst);

/*
 * In an ideal world, we would not export these low-level functions,
 * but this will probably take some time to fix properly
 */
extern u32 omap4_prminst_read_inst_reg(u8 part, s16 inst, u16 idx);
extern void omap4_prminst_write_inst_reg(u32 val, u8 part, s16 inst, u16 idx);
extern u32 omap4_prminst_rmw_inst_reg_bits(u32 mask, u32 bits, u8 part,
					   s16 inst, u16 idx);

extern void omap4_prminst_global_warm_sw_reset(void);

extern int omap4_prminst_is_hardreset_asserted(u8 shift, u8 part, s16 inst,
					       u16 rstctrl_offs);
extern int omap4_prminst_assert_hardreset(u8 shift, u8 part, s16 inst,
					  u16 rstctrl_offs);
int omap4_prminst_deassert_hardreset(u8 shift, u8 st_shift, u8 part,
				     s16 inst, u16 rstctrl_offs,
				     u16 rstst_offs);

extern void omap_prm_base_init(void);

#endif
