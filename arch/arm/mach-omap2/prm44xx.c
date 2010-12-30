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

#include <plat/common.h>
#include <plat/cpu.h>
#include <plat/prcm.h>

#include "prm.h"
#include "prm-regbits-44xx.h"

/*
 * Address offset (in bytes) between the reset control and the reset
 * status registers: 4 bytes on OMAP4
 */
#define OMAP4_RST_CTRL_ST_OFFSET		4

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

