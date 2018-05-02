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
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include "fsp2.h"

#define FSP2_BUS_ERR	"ibm,bus-error-irq"
#define FSP2_CMU_ERR	"ibm,cmu-error-irq"
#define FSP2_CONF_ERR	"ibm,conf-error-irq"
#define FSP2_OPBD_ERR	"ibm,opbd-error-irq"
#define FSP2_MCUE	"ibm,mc-ue-irq"
#define FSP2_RST_WRN	"ibm,reset-warning-irq"

static __initdata struct of_device_id fsp2_of_bus[] = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,plb6", },
	{ .compatible = "ibm,opb", },
	{},
};

static void l2regs(void)
{
	pr_err("L2 Controller:\n");
	pr_err("MCK:      0x%08x\n", mfl2(L2MCK));
	pr_err("INT:      0x%08x\n", mfl2(L2INT));
	pr_err("PLBSTAT0: 0x%08x\n", mfl2(L2PLBSTAT0));
	pr_err("PLBSTAT1: 0x%08x\n", mfl2(L2PLBSTAT1));
	pr_err("ARRSTAT0: 0x%08x\n", mfl2(L2ARRSTAT0));
	pr_err("ARRSTAT1: 0x%08x\n", mfl2(L2ARRSTAT1));
	pr_err("ARRSTAT2: 0x%08x\n", mfl2(L2ARRSTAT2));
	pr_err("CPUSTAT:  0x%08x\n", mfl2(L2CPUSTAT));
	pr_err("RACSTAT0: 0x%08x\n", mfl2(L2RACSTAT0));
	pr_err("WACSTAT0: 0x%08x\n", mfl2(L2WACSTAT0));
	pr_err("WACSTAT1: 0x%08x\n", mfl2(L2WACSTAT1));
	pr_err("WACSTAT2: 0x%08x\n", mfl2(L2WACSTAT2));
	pr_err("WDFSTAT:  0x%08x\n", mfl2(L2WDFSTAT));
	pr_err("LOG0:     0x%08x\n", mfl2(L2LOG0));
	pr_err("LOG1:     0x%08x\n", mfl2(L2LOG1));
	pr_err("LOG2:     0x%08x\n", mfl2(L2LOG2));
	pr_err("LOG3:     0x%08x\n", mfl2(L2LOG3));
	pr_err("LOG4:     0x%08x\n", mfl2(L2LOG4));
	pr_err("LOG5:     0x%08x\n", mfl2(L2LOG5));
}

static void show_plbopb_regs(u32 base, int num)
{
	pr_err("\nPLBOPB Bridge %d:\n", num);
	pr_err("GESR0: 0x%08x\n", mfdcr(base + PLB4OPB_GESR0));
	pr_err("GESR1: 0x%08x\n", mfdcr(base + PLB4OPB_GESR1));
	pr_err("GESR2: 0x%08x\n", mfdcr(base + PLB4OPB_GESR2));
	pr_err("GEARU: 0x%08x\n", mfdcr(base + PLB4OPB_GEARU));
	pr_err("GEAR:  0x%08x\n", mfdcr(base + PLB4OPB_GEAR));
}

static irqreturn_t bus_err_handler(int irq, void *data)
{
	pr_err("Bus Error\n");

	l2regs();

	pr_err("\nPLB6 Controller:\n");
	pr_err("BC_SHD: 0x%08x\n", mfdcr(DCRN_PLB6_SHD));
	pr_err("BC_ERR: 0x%08x\n", mfdcr(DCRN_PLB6_ERR));

	pr_err("\nPLB6-to-PLB4 Bridge:\n");
	pr_err("ESR:  0x%08x\n", mfdcr(DCRN_PLB6PLB4_ESR));
	pr_err("EARH: 0x%08x\n", mfdcr(DCRN_PLB6PLB4_EARH));
	pr_err("EARL: 0x%08x\n", mfdcr(DCRN_PLB6PLB4_EARL));

	pr_err("\nPLB4-to-PLB6 Bridge:\n");
	pr_err("ESR:  0x%08x\n", mfdcr(DCRN_PLB4PLB6_ESR));
	pr_err("EARH: 0x%08x\n", mfdcr(DCRN_PLB4PLB6_EARH));
	pr_err("EARL: 0x%08x\n", mfdcr(DCRN_PLB4PLB6_EARL));

	pr_err("\nPLB6-to-MCIF Bridge:\n");
	pr_err("BESR0: 0x%08x\n", mfdcr(DCRN_PLB6MCIF_BESR0));
	pr_err("BESR1: 0x%08x\n", mfdcr(DCRN_PLB6MCIF_BESR1));
	pr_err("BEARH: 0x%08x\n", mfdcr(DCRN_PLB6MCIF_BEARH));
	pr_err("BEARL: 0x%08x\n", mfdcr(DCRN_PLB6MCIF_BEARL));

	pr_err("\nPLB4 Arbiter:\n");
	pr_err("P0ESRH 0x%08x\n", mfdcr(DCRN_PLB4_P0ESRH));
	pr_err("P0ESRL 0x%08x\n", mfdcr(DCRN_PLB4_P0ESRL));
	pr_err("P0EARH 0x%08x\n", mfdcr(DCRN_PLB4_P0EARH));
	pr_err("P0EARH 0x%08x\n", mfdcr(DCRN_PLB4_P0EARH));
	pr_err("P1ESRH 0x%08x\n", mfdcr(DCRN_PLB4_P1ESRH));
	pr_err("P1ESRL 0x%08x\n", mfdcr(DCRN_PLB4_P1ESRL));
	pr_err("P1EARH 0x%08x\n", mfdcr(DCRN_PLB4_P1EARH));
	pr_err("P1EARH 0x%08x\n", mfdcr(DCRN_PLB4_P1EARH));

	show_plbopb_regs(DCRN_PLB4OPB0_BASE, 0);
	show_plbopb_regs(DCRN_PLB4OPB1_BASE, 1);
	show_plbopb_regs(DCRN_PLB4OPB2_BASE, 2);
	show_plbopb_regs(DCRN_PLB4OPB3_BASE, 3);

	pr_err("\nPLB4-to-AHB Bridge:\n");
	pr_err("ESR:   0x%08x\n", mfdcr(DCRN_PLB4AHB_ESR));
	pr_err("SEUAR: 0x%08x\n", mfdcr(DCRN_PLB4AHB_SEUAR));
	pr_err("SELAR: 0x%08x\n", mfdcr(DCRN_PLB4AHB_SELAR));

	pr_err("\nAHB-to-PLB4 Bridge:\n");
	pr_err("\nESR: 0x%08x\n", mfdcr(DCRN_AHBPLB4_ESR));
	pr_err("\nEAR: 0x%08x\n", mfdcr(DCRN_AHBPLB4_EAR));
	panic("Bus Error\n");
}

static irqreturn_t cmu_err_handler(int irq, void *data) {
	pr_err("CMU Error\n");
	pr_err("FIR0: 0x%08x\n", mfcmu(CMUN_FIR0));
	panic("CMU Error\n");
}

static irqreturn_t conf_err_handler(int irq, void *data) {
	pr_err("Configuration Logic Error\n");
	pr_err("CONF_FIR: 0x%08x\n", mfdcr(DCRN_CONF_FIR_RWC));
	pr_err("RPERR0:   0x%08x\n", mfdcr(DCRN_CONF_RPERR0));
	pr_err("RPERR1:   0x%08x\n", mfdcr(DCRN_CONF_RPERR1));
	panic("Configuration Logic Error\n");
}

static irqreturn_t opbd_err_handler(int irq, void *data) {
	panic("OPBD Error\n");
}

static irqreturn_t mcue_handler(int irq, void *data) {
	pr_err("DDR: Uncorrectable Error\n");
	pr_err("MCSTAT:            0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_MCSTAT));
	pr_err("MCOPT1:            0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_MCOPT1));
	pr_err("MCOPT2:            0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_MCOPT2));
	pr_err("PHYSTAT:           0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_PHYSTAT));
	pr_err("CFGR0:             0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_CFGR0));
	pr_err("CFGR1:             0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_CFGR1));
	pr_err("CFGR2:             0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_CFGR2));
	pr_err("CFGR3:             0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_CFGR3));
	pr_err("SCRUB_CNTL:        0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_SCRUB_CNTL));
	pr_err("ECCERR_PORT0:      0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_ECCERR_PORT0));
	pr_err("ECCERR_ADDR_PORT0: 0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_ECCERR_ADDR_PORT0));
	pr_err("ECCERR_CNT_PORT0:  0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_ECCERR_COUNT_PORT0));
	pr_err("ECC_CHECK_PORT0:   0x%08x\n",
		mfdcr(DCRN_DDR34_BASE + DCRN_DDR34_ECC_CHECK_PORT0));
	pr_err("MCER0:            0x%08x\n",
		mfdcr(DCRN_CW_BASE + DCRN_CW_MCER0));
	pr_err("MCER1:            0x%08x\n",
		mfdcr(DCRN_CW_BASE + DCRN_CW_MCER1));
	pr_err("BESR:             0x%08x\n",
		mfdcr(DCRN_PLB6MCIF_BESR0));
	pr_err("BEARL:            0x%08x\n",
		mfdcr(DCRN_PLB6MCIF_BEARL));
	pr_err("BEARH:            0x%08x\n",
		mfdcr(DCRN_PLB6MCIF_BEARH));
	panic("DDR: Uncorrectable Error\n");
}

static irqreturn_t rst_wrn_handler(int irq, void *data) {
	u32 crcs = mfcmu(CMUN_CRCS);
	switch (crcs & CRCS_STAT_MASK) {
	case CRCS_STAT_CHIP_RST_B:
		panic("Received chassis-initiated reset request");
	default:
		panic("Unknown external reset: CRCS=0x%x", crcs);
	}
}

static void node_irq_request(const char *compat, irq_handler_t errirq_handler)
{
	struct device_node *np;
	unsigned int irq;
	int32_t rc;

	for_each_compatible_node(np, NULL, compat) {
		irq = irq_of_parse_and_map(np, 0);
		if (irq == NO_IRQ) {
			pr_err("device tree node %s is missing a interrupt",
			      np->name);
			return;
		}

		rc = request_irq(irq, errirq_handler, 0, np->name, np);
		if (rc) {
			pr_err("fsp_of_probe: request_irq failed: np=%s rc=%d",
			      np->full_name, rc);
			return;
		}
	}
}

static void critical_irq_setup(void)
{
	node_irq_request(FSP2_CMU_ERR, cmu_err_handler);
	node_irq_request(FSP2_BUS_ERR, bus_err_handler);
	node_irq_request(FSP2_CONF_ERR, conf_err_handler);
	node_irq_request(FSP2_OPBD_ERR, opbd_err_handler);
	node_irq_request(FSP2_MCUE, mcue_handler);
	node_irq_request(FSP2_RST_WRN, rst_wrn_handler);
}

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

static void __init fsp2_irq_init(void)
{
	uic_init_tree();
	critical_irq_setup();
}

define_machine(fsp2) {
	.name			= "FSP-2",
	.probe			= fsp2_probe,
	.progress		= udbg_progress,
	.init_IRQ		= fsp2_irq_init,
	.get_irq		= uic_get_irq,
	.restart		= ppc4xx_reset_system,
	.calibrate_decr		= generic_calibrate_decr,
};
