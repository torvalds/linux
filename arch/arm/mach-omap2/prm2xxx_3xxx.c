/*
 * OMAP2/3 PRM module functions
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
#include "prm-regbits-24xx.h"
#include "prm-regbits-34xx.h"

/**
 * omap2_prm_is_hardreset_asserted - read the HW reset line state of
 * submodules contained in the hwmod module
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @shift: register bit shift corresponding to the reset line to check
 *
 * Returns 1 if the (sub)module hardreset line is currently asserted,
 * 0 if the (sub)module hardreset line is not currently asserted, or
 * -EINVAL if called while running on a non-OMAP2/3 chip.
 */
int omap2_prm_is_hardreset_asserted(s16 prm_mod, u8 shift)
{
	if (!(cpu_is_omap24xx() || cpu_is_omap34xx()))
		return -EINVAL;

	return prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTCTRL,
				       (1 << shift));
}

/**
 * omap2_prm_assert_hardreset - assert the HW reset line of a submodule
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @shift: register bit shift corresponding to the reset line to assert
 *
 * Some IPs like dsp or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * place the submodule into reset.  Returns 0 upon success or -EINVAL
 * upon an argument error.
 */
int omap2_prm_assert_hardreset(s16 prm_mod, u8 shift)
{
	u32 mask;

	if (!(cpu_is_omap24xx() || cpu_is_omap34xx()))
		return -EINVAL;

	mask = 1 << shift;
	prm_rmw_mod_reg_bits(mask, mask, prm_mod, OMAP2_RM_RSTCTRL);

	return 0;
}

/**
 * omap2_prm_deassert_hardreset - deassert a submodule hardreset line and wait
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @shift: register bit shift corresponding to the reset line to deassert
 *
 * Some IPs like dsp or iva contain processors that require an HW
 * reset line to be asserted / deasserted in order to fully enable the
 * IP.  These modules may have multiple hard-reset lines that reset
 * different 'submodules' inside the IP block.  This function will
 * take the submodule out of reset and wait until the PRCM indicates
 * that the reset has completed before returning.  Returns 0 upon success or
 * -EINVAL upon an argument error, -EEXIST if the submodule was already out
 * of reset, or -EBUSY if the submodule did not exit reset promptly.
 */
int omap2_prm_deassert_hardreset(s16 prm_mod, u8 shift)
{
	u32 mask;
	int c;

	if (!(cpu_is_omap24xx() || cpu_is_omap34xx()))
		return -EINVAL;

	mask = 1 << shift;

	/* Check the current status to avoid de-asserting the line twice */
	if (prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTCTRL, mask) == 0)
		return -EEXIST;

	/* Clear the reset status by writing 1 to the status bit */
	prm_rmw_mod_reg_bits(0xffffffff, mask, prm_mod, OMAP2_RM_RSTST);
	/* de-assert the reset control line */
	prm_rmw_mod_reg_bits(mask, 0, prm_mod, OMAP2_RM_RSTCTRL);
	/* wait the status to be set */
	omap_test_timeout(prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTST,
						  mask),
			  MAX_MODULE_HARDRESET_WAIT, c);

	return (c == MAX_MODULE_HARDRESET_WAIT) ? -EBUSY : 0;
}

