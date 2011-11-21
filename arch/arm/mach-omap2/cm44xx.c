/*
 * OMAP4 CM1, CM2 module low-level functions
 *
 * Copyright (C) 2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * These functions are intended to be used only by the cminst44xx.c file.
 * XXX Perhaps we should just move them there and make them static.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "common.h"

#include "cm.h"
#include "cm1_44xx.h"
#include "cm2_44xx.h"
#include "cm-regbits-44xx.h"

/* CM1 hardware module low-level functions */

/* Read a register in CM1 */
u32 omap4_cm1_read_inst_reg(s16 inst, u16 reg)
{
	return __raw_readl(OMAP44XX_CM1_REGADDR(inst, reg));
}

/* Write into a register in CM1 */
void omap4_cm1_write_inst_reg(u32 val, s16 inst, u16 reg)
{
	__raw_writel(val, OMAP44XX_CM1_REGADDR(inst, reg));
}

/* Read a register in CM2 */
u32 omap4_cm2_read_inst_reg(s16 inst, u16 reg)
{
	return __raw_readl(OMAP44XX_CM2_REGADDR(inst, reg));
}

/* Write into a register in CM2 */
void omap4_cm2_write_inst_reg(u32 val, s16 inst, u16 reg)
{
	__raw_writel(val, OMAP44XX_CM2_REGADDR(inst, reg));
}
