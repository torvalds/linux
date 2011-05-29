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
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <plat/common.h>
#include <plat/cpu.h>
#include <plat/prcm.h>

#include "prm2xxx_3xxx.h"
#include "cm2xxx_3xxx.h"
#include "prm-regbits-24xx.h"
#include "prm-regbits-34xx.h"

u32 omap2_prm_read_mod_reg(s16 module, u16 idx)
{
	return __raw_readl(prm_base + module + idx);
}

void omap2_prm_write_mod_reg(u32 val, s16 module, u16 idx)
{
	__raw_writel(val, prm_base + module + idx);
}

/* Read-modify-write a register in a PRM module. Caller must lock */
u32 omap2_prm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx)
{
	u32 v;

	v = omap2_prm_read_mod_reg(module, idx);
	v &= ~mask;
	v |= bits;
	omap2_prm_write_mod_reg(v, module, idx);

	return v;
}

/* Read a PRM register, AND it, and shift the result down to bit 0 */
u32 omap2_prm_read_mod_bits_shift(s16 domain, s16 idx, u32 mask)
{
	u32 v;

	v = omap2_prm_read_mod_reg(domain, idx);
	v &= mask;
	v >>= __ffs(mask);

	return v;
}

u32 omap2_prm_set_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return omap2_prm_rmw_mod_reg_bits(bits, bits, module, idx);
}

u32 omap2_prm_clear_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return omap2_prm_rmw_mod_reg_bits(bits, 0x0, module, idx);
}


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

	return omap2_prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTCTRL,
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
	omap2_prm_rmw_mod_reg_bits(mask, mask, prm_mod, OMAP2_RM_RSTCTRL);

	return 0;
}

/**
 * omap2_prm_deassert_hardreset - deassert a submodule hardreset line and wait
 * @prm_mod: PRM submodule base (e.g. CORE_MOD)
 * @rst_shift: register bit shift corresponding to the reset line to deassert
 * @st_shift: register bit shift for the status of the deasserted submodule
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
int omap2_prm_deassert_hardreset(s16 prm_mod, u8 rst_shift, u8 st_shift)
{
	u32 rst, st;
	int c;

	if (!(cpu_is_omap24xx() || cpu_is_omap34xx()))
		return -EINVAL;

	rst = 1 << rst_shift;
	st = 1 << st_shift;

	/* Check the current status to avoid de-asserting the line twice */
	if (omap2_prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTCTRL, rst) == 0)
		return -EEXIST;

	/* Clear the reset status by writing 1 to the status bit */
	omap2_prm_rmw_mod_reg_bits(0xffffffff, st, prm_mod, OMAP2_RM_RSTST);
	/* de-assert the reset control line */
	omap2_prm_rmw_mod_reg_bits(rst, 0, prm_mod, OMAP2_RM_RSTCTRL);
	/* wait the status to be set */
	omap_test_timeout(omap2_prm_read_mod_bits_shift(prm_mod, OMAP2_RM_RSTST,
						  st),
			  MAX_MODULE_HARDRESET_WAIT, c);

	return (c == MAX_MODULE_HARDRESET_WAIT) ? -EBUSY : 0;
}
