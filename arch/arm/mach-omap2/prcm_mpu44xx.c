// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP4 PRCM_MPU module functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "iomap.h"
#include "common.h"
#include "prcm_mpu44xx.h"
#include "cm-regbits-44xx.h"

/*
 * prcm_mpu_base: the virtual address of the start of the PRCM_MPU IP
 *   block registers
 */
struct omap_domain_base prcm_mpu_base;

/* PRCM_MPU low-level functions */

u32 omap4_prcm_mpu_read_inst_reg(s16 inst, u16 reg)
{
	return readl_relaxed(OMAP44XX_PRCM_MPU_REGADDR(inst, reg));
}

void omap4_prcm_mpu_write_inst_reg(u32 val, s16 inst, u16 reg)
{
	writel_relaxed(val, OMAP44XX_PRCM_MPU_REGADDR(inst, reg));
}

/**
 * omap2_set_globals_prcm_mpu - set the MPU PRCM base address (for early use)
 * @prcm_mpu: PRCM_MPU base virtual address
 *
 * XXX Will be replaced when the PRM/CM drivers are completed.
 */
void __init omap2_set_globals_prcm_mpu(void __iomem *prcm_mpu)
{
	prcm_mpu_base.va = prcm_mpu;
}
