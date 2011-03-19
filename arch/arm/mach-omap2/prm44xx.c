/*
 * OMAP4 PRM module functions
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
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

/*
 * Address offset (in bytes) between the reset control and the reset
 * status registers: 4 bytes on OMAP4
 */
#define OMAP4_RST_CTRL_ST_OFFSET		4

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

/**
 * omap4_prm_is_hardreset_asserted - read the HW reset line state of
 * submodules contained in the hwmod module
 * @rstctrl_reg: RM_RSTCTRL register address for this module
 * @shift: register bit shift corresponding to the reset line to check
 *
 * Returns 1 if the (sub)module hardreset line is currently asserted,
 * 0 if the (sub)module hardreset line is not currently asserted, or
 * -EINVAL upon parameter error.
 */
int omap4_prm_is_hardreset_asserted(void __iomem *rstctrl_reg, u8 shift)
{
	if (!cpu_is_omap44xx() || !rstctrl_reg)
		return -EINVAL;

	return omap4_prm_read_bits_shift(rstctrl_reg, (1 << shift));
}

/**
 * omap4_prm_assert_hardreset - assert the HW reset line of a submodule
 * @rstctrl_reg: RM_RSTCTRL register address for this module
 * @shift: register bit shift corresponding to the reset line to assert
 *
 * Some IPs like dsp, ipu or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * place the submodule into reset.  Returns 0 upon success or -EINVAL
 * upon an argument error.
 */
int omap4_prm_assert_hardreset(void __iomem *rstctrl_reg, u8 shift)
{
	u32 mask;

	if (!cpu_is_omap44xx() || !rstctrl_reg)
		return -EINVAL;

	mask = 1 << shift;
	omap4_prm_rmw_reg_bits(mask, mask, rstctrl_reg);

	return 0;
}

/**
 * omap4_prm_deassert_hardreset - deassert a submodule hardreset line and wait
 * @rstctrl_reg: RM_RSTCTRL register address for this module
 * @shift: register bit shift corresponding to the reset line to deassert
 *
 * Some IPs like dsp, ipu or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * take the submodule out of reset and wait until the PRCM indicates
 * that the reset has completed before returning.  Returns 0 upon success or
 * -EINVAL upon an argument error, -EEXIST if the submodule was already out
 * of reset, or -EBUSY if the submodule did not exit reset promptly.
 */
int omap4_prm_deassert_hardreset(void __iomem *rstctrl_reg, u8 shift)
{
	u32 mask;
	void __iomem *rstst_reg;
	int c;

	if (!cpu_is_omap44xx() || !rstctrl_reg)
		return -EINVAL;

	rstst_reg = rstctrl_reg + OMAP4_RST_CTRL_ST_OFFSET;

	mask = 1 << shift;

	/* Check the current status to avoid de-asserting the line twice */
	if (omap4_prm_read_bits_shift(rstctrl_reg, mask) == 0)
		return -EEXIST;

	/* Clear the reset status by writing 1 to the status bit */
	omap4_prm_rmw_reg_bits(0xffffffff, mask, rstst_reg);
	/* de-assert the reset control line */
	omap4_prm_rmw_reg_bits(mask, 0, rstctrl_reg);
	/* wait the status to be set */
	omap_test_timeout(omap4_prm_read_bits_shift(rstst_reg, mask),
			  MAX_MODULE_HARDRESET_WAIT, c);

	return (c == MAX_MODULE_HARDRESET_WAIT) ? -EBUSY : 0;
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
