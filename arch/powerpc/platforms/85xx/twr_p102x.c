// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2010-2011, 2013 Freescale Semiconductor, Inc.
 *
 * Author: Michael Johnston <michael.johnston@freescale.com>
 *
 * Description:
 * TWR-P102x Board Setup
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fsl/guts.h>
#include <linux/pci.h>
#include <linux/of_platform.h>

#include <asm/pci-bridge.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <soc/fsl/qe/qe.h>
#include <soc/fsl/qe/qe_ic.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include "smp.h"

#include "mpc85xx.h"

static void __init twr_p1025_pic_init(void)
{
	struct mpic *mpic;

#ifdef CONFIG_QUICC_ENGINE
	struct device_node *np;
#endif

	mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
			MPIC_SINGLE_DEST_CPU,
			0, 256, " OpenPIC  ");

	BUG_ON(mpic == NULL);
	mpic_init(mpic);

#ifdef CONFIG_QUICC_ENGINE
	np = of_find_compatible_node(NULL, NULL, "fsl,qe-ic");
	if (np) {
		qe_ic_init(np, 0, qe_ic_cascade_low_mpic,
				qe_ic_cascade_high_mpic);
		of_node_put(np);
	} else
		pr_err("Could not find qe-ic node\n");
#endif
}

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init twr_p1025_setup_arch(void)
{
#ifdef CONFIG_QUICC_ENGINE
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("twr_p1025_setup_arch()", 0);

	mpc85xx_smp_init();

	fsl_pci_assign_primary();

#ifdef CONFIG_QUICC_ENGINE
	mpc85xx_qe_par_io_init();

#if IS_ENABLED(CONFIG_UCC_GETH) || IS_ENABLED(CONFIG_SERIAL_QE)
	if (machine_is(twr_p1025)) {
		struct ccsr_guts __iomem *guts;

		np = of_find_compatible_node(NULL, NULL, "fsl,p1021-guts");
		if (np) {
			guts = of_iomap(np, 0);
			if (!guts)
				pr_err("twr_p1025: could not map global utilities register\n");
			else {
			/* P1025 has pins muxed for QE and other functions. To
			 * enable QE UEC mode, we need to set bit QE0 for UCC1
			 * in Eth mode, QE0 and QE3 for UCC5 in Eth mode, QE9
			 * and QE12 for QE MII management signals in PMUXCR
			 * register.
			 * Set QE mux bits in PMUXCR */
			setbits32(&guts->pmuxcr, MPC85xx_PMUXCR_QE(0) |
					MPC85xx_PMUXCR_QE(3) |
					MPC85xx_PMUXCR_QE(9) |
					MPC85xx_PMUXCR_QE(12));
			iounmap(guts);

#if IS_ENABLED(CONFIG_SERIAL_QE)
			/* On P1025TWR board, the UCC7 acted as UART port.
			 * However, The UCC7's CTS pin is low level in default,
			 * it will impact the transmission in full duplex
			 * communication. So disable the Flow control pin PA18.
			 * The UCC7 UART just can use RXD and TXD pins.
			 */
			par_io_config_pin(0, 18, 0, 0, 0, 0);
#endif
			/* Drive PB29 to CPLD low - CPLD will then change
			 * muxing from LBC to QE */
			par_io_config_pin(1, 29, 1, 0, 0, 0);
			par_io_data_set(1, 29, 0);
			}
			of_node_put(np);
		}
	}
#endif
#endif	/* CONFIG_QUICC_ENGINE */

	pr_info("TWR-P1025 board from Freescale Semiconductor\n");
}

machine_arch_initcall(twr_p1025, mpc85xx_common_publish_devices);

static int __init twr_p1025_probe(void)
{
	return of_machine_is_compatible("fsl,TWR-P1025");
}

define_machine(twr_p1025) {
	.name			= "TWR-P1025",
	.probe			= twr_p1025_probe,
	.setup_arch		= twr_p1025_setup_arch,
	.init_IRQ		= twr_p1025_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
