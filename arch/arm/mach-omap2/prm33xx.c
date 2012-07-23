/*
 * AM33XX PRM functions
 *
 * Copyright (C) 2011-2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <plat/common.h>

#include "common.h"
#include "prm33xx.h"
#include "prm-regbits-33xx.h"

/* Read a register in a PRM instance */
u32 am33xx_prm_read_reg(s16 inst, u16 idx)
{
	return __raw_readl(prm_base + inst + idx);
}

/* Write into a register in a PRM instance */
void am33xx_prm_write_reg(u32 val, s16 inst, u16 idx)
{
	__raw_writel(val, prm_base + inst + idx);
}

/* Read-modify-write a register in PRM. Caller must lock */
u32 am33xx_prm_rmw_reg_bits(u32 mask, u32 bits, s16 inst, s16 idx)
{
	u32 v;

	v = am33xx_prm_read_reg(inst, idx);
	v &= ~mask;
	v |= bits;
	am33xx_prm_write_reg(v, inst, idx);

	return v;
}

/**
 * am33xx_prm_is_hardreset_asserted - read the HW reset line state of
 * submodules contained in the hwmod module
 * @shift: register bit shift corresponding to the reset line to check
 * @inst: CM instance register offset (*_INST macro)
 * @rstctrl_offs: RM_RSTCTRL register address offset for this module
 *
 * Returns 1 if the (sub)module hardreset line is currently asserted,
 * 0 if the (sub)module hardreset line is not currently asserted, or
 * -EINVAL upon parameter error.
 */
int am33xx_prm_is_hardreset_asserted(u8 shift, s16 inst, u16 rstctrl_offs)
{
	u32 v;

	v = am33xx_prm_read_reg(inst, rstctrl_offs);
	v &= 1 << shift;
	v >>= shift;

	return v;
}

/**
 * am33xx_prm_assert_hardreset - assert the HW reset line of a submodule
 * @shift: register bit shift corresponding to the reset line to assert
 * @inst: CM instance register offset (*_INST macro)
 * @rstctrl_reg: RM_RSTCTRL register address for this module
 *
 * Some IPs like dsp, ipu or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * place the submodule into reset.  Returns 0 upon success or -EINVAL
 * upon an argument error.
 */
int am33xx_prm_assert_hardreset(u8 shift, s16 inst, u16 rstctrl_offs)
{
	u32 mask = 1 << shift;

	am33xx_prm_rmw_reg_bits(mask, mask, inst, rstctrl_offs);

	return 0;
}

/**
 * am33xx_prm_deassert_hardreset - deassert a submodule hardreset line and
 * wait
 * @shift: register bit shift corresponding to the reset line to deassert
 * @inst: CM instance register offset (*_INST macro)
 * @rstctrl_reg: RM_RSTCTRL register address for this module
 * @rstst_reg: RM_RSTST register address for this module
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
int am33xx_prm_deassert_hardreset(u8 shift, s16 inst,
		u16 rstctrl_offs, u16 rstst_offs)
{
	int c;
	u32 mask = 1 << shift;

	/* Check the current status to avoid  de-asserting the line twice */
	if (am33xx_prm_is_hardreset_asserted(shift, inst, rstctrl_offs) == 0)
		return -EEXIST;

	/* Clear the reset status by writing 1 to the status bit */
	am33xx_prm_rmw_reg_bits(0xffffffff, mask, inst, rstst_offs);
	/* de-assert the reset control line */
	am33xx_prm_rmw_reg_bits(mask, 0, inst, rstctrl_offs);
	/* wait the status to be set */

	omap_test_timeout(am33xx_prm_is_hardreset_asserted(shift, inst,
							   rstst_offs),
			  MAX_MODULE_HARDRESET_WAIT, c);

	return (c == MAX_MODULE_HARDRESET_WAIT) ? -EBUSY : 0;
}
