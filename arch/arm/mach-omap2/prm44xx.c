/*
 * OMAP4 PRM module functions
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 * Benoît Cousson
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <plat/common.h>
#include <plat/cpu.h>
#include <plat/prcm.h>

#include "prm44xx.h"
#include "prm-regbits-44xx.h"

/* PRM low-level functions */

/* Read a register in a CM/PRM instance in the PRM module */
u32 omap4_prm_read_inst_reg(s16 inst, u16 reg)
{
	return __raw_readl(OMAP44XX_PRM_REGADDR(inst, reg));
}

/* Write into a register in a CM/PRM instance in the PRM module */
void omap4_prm_write_inst_reg(u32 val, s16 inst, u16 reg)
{
	__raw_writel(val, OMAP44XX_PRM_REGADDR(inst, reg));
}

/* Read-modify-write a register in a PRM module. Caller must lock */
u32 omap4_prm_rmw_inst_reg_bits(u32 mask, u32 bits, s16 inst, s16 reg)
{
	u32 v;

	v = omap4_prm_read_inst_reg(inst, reg);
	v &= ~mask;
	v |= bits;
	omap4_prm_write_inst_reg(v, inst, reg);

	return v;
}

/* Read a PRM register, AND it, and shift the result down to bit 0 */
/* XXX deprecated */
u32 omap4_prm_read_bits_shift(void __iomem *reg, u32 mask)
{
	u32 v;

	v = __raw_readl(reg);
	v &= mask;
	v >>= __ffs(mask);

	return v;
}

/* Read-modify-write a register in a PRM module. Caller must lock */
/* XXX deprecated */
u32 omap4_prm_rmw_reg_bits(u32 mask, u32 bits, void __iomem *reg)
{
	u32 v;

	v = __raw_readl(reg);
	v &= ~mask;
	v |= bits;
	__raw_writel(v, reg);

	return v;
}

u32 omap4_prm_set_inst_reg_bits(u32 bits, s16 inst, s16 reg)
{
	return omap4_prm_rmw_inst_reg_bits(bits, bits, inst, reg);
}

u32 omap4_prm_clear_inst_reg_bits(u32 bits, s16 inst, s16 reg)
{
	return omap4_prm_rmw_inst_reg_bits(bits, 0x0, inst, reg);
}

void omap4_prm_global_warm_sw_reset(void)
{
	u32 v;

	v = omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST,
				    OMAP4_RM_RSTCTRL);
	v |= OMAP4430_RST_GLOBAL_WARM_SW_MASK;
	omap4_prm_write_inst_reg(v, OMAP4430_PRM_DEVICE_INST,
				 OMAP4_RM_RSTCTRL);

	/* OCP barrier */
	v = omap4_prm_read_inst_reg(OMAP4430_PRM_DEVICE_INST,
				    OMAP4_RM_RSTCTRL);
}
