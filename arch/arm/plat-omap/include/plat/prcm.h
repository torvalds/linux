/*
 * arch/arm/plat-omap/include/mach/prcm.h
 *
 * Access definations for use in OMAP24XX clock and power management
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ASM_ARM_ARCH_OMAP_PRCM_H
#define __ASM_ARM_ARCH_OMAP_PRCM_H

u32 omap_prcm_get_reset_sources(void);
void omap_prcm_arch_reset(char mode, const char *cmd);
int omap2_cm_wait_idlest(void __iomem *reg, u32 mask, u8 idlest,
			 const char *name);

#define START_PADCONF_SAVE 0x2
#define PADCONF_SAVE_DONE  0x1

void omap3_prcm_save_context(void);
void omap3_prcm_restore_context(void);

u32 prm_read_mod_reg(s16 module, u16 idx);
void prm_write_mod_reg(u32 val, s16 module, u16 idx);
u32 prm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx);
u32 prm_read_mod_bits_shift(s16 domain, s16 idx, u32 mask);
u32 cm_read_mod_reg(s16 module, u16 idx);
void cm_write_mod_reg(u32 val, s16 module, u16 idx);
u32 cm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx);

#endif



