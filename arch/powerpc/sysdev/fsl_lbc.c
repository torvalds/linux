/*
 * Freescale LBC and UPM routines.
 *
 * Copyright (c) 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/of.h>
#include <asm/prom.h>
#include <asm/fsl_lbc.h>

static spinlock_t fsl_lbc_lock = __SPIN_LOCK_UNLOCKED(fsl_lbc_lock);
static struct fsl_lbc_regs __iomem *fsl_lbc_regs;

static char __initdata *compat_lbc[] = {
	"fsl,pq2-localbus",
	"fsl,pq2pro-localbus",
	"fsl,pq3-localbus",
	"fsl,elbc",
};

static int __init fsl_lbc_init(void)
{
	struct device_node *lbus;
	int i;

	for (i = 0; i < ARRAY_SIZE(compat_lbc); i++) {
		lbus = of_find_compatible_node(NULL, NULL, compat_lbc[i]);
		if (lbus)
			goto found;
	}
	return -ENODEV;

found:
	fsl_lbc_regs = of_iomap(lbus, 0);
	of_node_put(lbus);
	if (!fsl_lbc_regs)
		return -ENOMEM;
	return 0;
}
arch_initcall(fsl_lbc_init);

/**
 * fsl_lbc_find - find Localbus bank
 * @addr_base:	base address of the memory bank
 *
 * This function walks LBC banks comparing "Base address" field of the BR
 * registers with the supplied addr_base argument. When bases match this
 * function returns bank number (starting with 0), otherwise it returns
 * appropriate errno value.
 */
int fsl_lbc_find(phys_addr_t addr_base)
{
	int i;

	if (!fsl_lbc_regs)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(fsl_lbc_regs->bank); i++) {
		__be32 br = in_be32(&fsl_lbc_regs->bank[i].br);
		__be32 or = in_be32(&fsl_lbc_regs->bank[i].or);

		if (br & BR_V && (br & or & BR_BA) == addr_base)
			return i;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(fsl_lbc_find);

/**
 * fsl_upm_find - find pre-programmed UPM via base address
 * @addr_base:	base address of the memory bank controlled by the UPM
 * @upm:	pointer to the allocated fsl_upm structure
 *
 * This function fills fsl_upm structure so you can use it with the rest of
 * UPM API. On success this function returns 0, otherwise it returns
 * appropriate errno value.
 */
int fsl_upm_find(phys_addr_t addr_base, struct fsl_upm *upm)
{
	int bank;
	__be32 br;

	bank = fsl_lbc_find(addr_base);
	if (bank < 0)
		return bank;

	br = in_be32(&fsl_lbc_regs->bank[bank].br);

	switch (br & BR_MSEL) {
	case BR_MS_UPMA:
		upm->mxmr = &fsl_lbc_regs->mamr;
		break;
	case BR_MS_UPMB:
		upm->mxmr = &fsl_lbc_regs->mbmr;
		break;
	case BR_MS_UPMC:
		upm->mxmr = &fsl_lbc_regs->mcmr;
		break;
	default:
		return -EINVAL;
	}

	switch (br & BR_PS) {
	case BR_PS_8:
		upm->width = 8;
		break;
	case BR_PS_16:
		upm->width = 16;
		break;
	case BR_PS_32:
		upm->width = 32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(fsl_upm_find);

/**
 * fsl_upm_run_pattern - actually run an UPM pattern
 * @upm:	pointer to the fsl_upm structure obtained via fsl_upm_find
 * @io_base:	remapped pointer to where memory access should happen
 * @mar:	MAR register content during pattern execution
 *
 * This function triggers dummy write to the memory specified by the io_base,
 * thus UPM pattern actually executed. Note that mar usage depends on the
 * pre-programmed AMX bits in the UPM RAM.
 */
int fsl_upm_run_pattern(struct fsl_upm *upm, void __iomem *io_base, u32 mar)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&fsl_lbc_lock, flags);

	out_be32(&fsl_lbc_regs->mar, mar << (32 - upm->width));

	switch (upm->width) {
	case 8:
		out_8(io_base, 0x0);
		break;
	case 16:
		out_be16(io_base, 0x0);
		break;
	case 32:
		out_be32(io_base, 0x0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock_irqrestore(&fsl_lbc_lock, flags);

	return ret;
}
EXPORT_SYMBOL(fsl_upm_run_pattern);
