/*
 * FSP-2 board specific routines
 *
 * Based on earlier code:
 *    Matt Porter <mporter@kernel.crashing.org>
 *    Copyright 2002-2005 MontaVista Software Inc.
 *
 *    Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *    Copyright (c) 2003-2005 Zultys Technologies
 *
 *    Rewritten and ported to the merged powerpc tree:
 *    Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/rtc.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/ppc4xx.h>
#include <asm/dcr.h>
#include "fsp2.h"

static __initdata struct of_device_id fsp2_of_bus[] = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,plb6", },
	{ .compatible = "ibm,opb", },
	{},
};

static int __init fsp2_device_probe(void)
{
	of_platform_bus_probe(NULL, fsp2_of_bus, NULL);
	return 0;
}
machine_device_initcall(fsp2, fsp2_device_probe);

static int __init fsp2_probe(void)
{
	u32 val;
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,fsp2"))
		return 0;

	/* Clear BC_ERR and mask snoopable request plb errors. */
	val = mfdcr(DCRN_PLB6_CR0);
	val |= 0x20000000;
	mtdcr(DCRN_PLB6_BASE, val);
	mtdcr(DCRN_PLB6_HD, 0xffff0000);
	mtdcr(DCRN_PLB6_SHD, 0xffff0000);

	/* TVSENSE reset is blocked (clock gated) by the POR default of the TVS
	 * sleep config bit. As a consequence, TVSENSE will provide erratic
	 * sensor values, which may result in spurious (parity) errors
	 * recorded in the CMU FIR and leading to erroneous interrupt requests
	 * once the CMU interrupt is unmasked.
	 */

	/* 1. set TVS1[UNDOZE] */
	val = mfcmu(CMUN_TVS1);
	val |= 0x4;
	mtcmu(CMUN_TVS1, val);

	/* 2. clear FIR[TVS] and FIR[TVSPAR] */
	val = mfcmu(CMUN_FIR0);
	val |= 0x30000000;
	mtcmu(CMUN_FIR0, val);

	/* L2 machine checks */
	mtl2(L2PLBMCKEN0, 0xffffffff);
	mtl2(L2PLBMCKEN1, 0x0000ffff);
	mtl2(L2ARRMCKEN0, 0xffffffff);
	mtl2(L2ARRMCKEN1, 0xffffffff);
	mtl2(L2ARRMCKEN2, 0xfffff000);
	mtl2(L2CPUMCKEN,  0xffffffff);
	mtl2(L2RACMCKEN0, 0xffffffff);
	mtl2(L2WACMCKEN0, 0xffffffff);
	mtl2(L2WACMCKEN1, 0xffffffff);
	mtl2(L2WACMCKEN2, 0xffffffff);
	mtl2(L2WDFMCKEN,  0xffffffff);

	/* L2 interrupts */
	mtl2(L2PLBINTEN1, 0xffff0000);

	/*
	 * At a global level, enable all L2 machine checks and interrupts
	 * reported by the L2 subsystems, except for the external machine check
	 * input (UIC0.1).
	 */
	mtl2(L2MCKEN, 0x000007ff);
	mtl2(L2INTEN, 0x000004ff);

	/* Enable FSP-2 configuration logic parity errors */
	mtdcr(DCRN_CONF_EIR_RS, 0x80000000);
	return 1;
}

define_machine(fsp2) {
	.name			= "FSP-2",
	.probe			= fsp2_probe,
	.progress		= udbg_progress,
	.init_IRQ		= uic_init_tree,
	.get_irq		= uic_get_irq,
	.restart		= ppc4xx_reset_system,
	.calibrate_decr		= generic_calibrate_decr,
};
