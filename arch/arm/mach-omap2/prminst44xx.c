/*
 * OMAP4 PRM instance functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <plat/common.h>

#include "prm44xx.h"
#include "prminst44xx.h"
#include "prm-regbits-44xx.h"
#include "prcm44xx.h"
#include "prcm_mpu44xx.h"

static u32 _prm_bases[OMAP4_MAX_PRCM_PARTITIONS] = {
	[OMAP4430_INVALID_PRCM_PARTITION]	= 0,
	[OMAP4430_PRM_PARTITION]		= OMAP4430_PRM_BASE,
	[OMAP4430_CM1_PARTITION]		= 0,
	[OMAP4430_CM2_PARTITION]		= 0,
	[OMAP4430_SCRM_PARTITION]		= 0,
	[OMAP4430_PRCM_MPU_PARTITION]		= OMAP4430_PRCM_MPU_BASE,
};

/* Read a register in a PRM instance */
u32 omap4_prminst_read_inst_reg(u8 part, s16 inst, u16 idx)
{
	BUG_ON(part >= OMAP4_MAX_PRCM_PARTITIONS ||
	       part == OMAP4430_INVALID_PRCM_PARTITION ||
	       !_prm_bases[part]);
	return __raw_readl(OMAP2_L4_IO_ADDRESS(_prm_bases[part] + inst +
					       idx));
}

/* Write into a register in a PRM instance */
void omap4_prminst_write_inst_reg(u32 val, u8 part, s16 inst, u16 idx)
{
	BUG_ON(part >= OMAP4_MAX_PRCM_PARTITIONS ||
	       part == OMAP4430_INVALID_PRCM_PARTITION ||
	       !_prm_bases[part]);
	__raw_writel(val, OMAP2_L4_IO_ADDRESS(_prm_bases[part] + inst + idx));
}

/* Read-modify-write a register in PRM. Caller must lock */
u32 omap4_prminst_rmw_inst_reg_bits(u32 mask, u32 bits, u8 part, s16 inst,
				   s16 idx)
{
	u32 v;

	v = omap4_prminst_read_inst_reg(part, inst, idx);
	v &= ~mask;
	v |= bits;
	omap4_prminst_write_inst_reg(v, part, inst, idx);

	return v;
}
