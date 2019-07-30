// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Board setup routines for the Emerson/Artesyn MVME7100
 *
 * Copyright 2016 Elettra-Sincrotrone Trieste S.C.p.A.
 *
 * Author: Alessio Igor Bogani <alessio.bogani@elettra.eu>
 *
 * Based on earlier code by:
 *
 *	Ajit Prem <ajit.prem@emerson.com>
 *	Copyright 2008 Emerson
 *
 * USB host fixup is borrowed by:
 *
 *	Martyn Welch <martyn.welch@ge.com>
 *	Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 */

#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc86xx.h"

#define MVME7100_INTERRUPT_REG_2_OFFSET	0x05
#define MVME7100_DS1375_MASK		0x40
#define MVME7100_MAX6649_MASK		0x20
#define MVME7100_ABORT_MASK		0x10

/*
 * Setup the architecture
 */
static void __init mvme7100_setup_arch(void)
{
	struct device_node *bcsr_node;
	void __iomem *mvme7100_regs = NULL;
	u8 reg;

	if (ppc_md.progress)
		ppc_md.progress("mvme7100_setup_arch()", 0);

#ifdef CONFIG_SMP
	mpc86xx_smp_init();
#endif

	fsl_pci_assign_primary();

	/* Remap BCSR registers */
	bcsr_node = of_find_compatible_node(NULL, NULL,
			"artesyn,mvme7100-bcsr");
	if (bcsr_node) {
		mvme7100_regs = of_iomap(bcsr_node, 0);
		of_node_put(bcsr_node);
	}

	if (mvme7100_regs) {
		/* Disable ds1375, max6649, and abort interrupts */
		reg = readb(mvme7100_regs + MVME7100_INTERRUPT_REG_2_OFFSET);
		reg |= MVME7100_DS1375_MASK | MVME7100_MAX6649_MASK
			| MVME7100_ABORT_MASK;
		writeb(reg, mvme7100_regs + MVME7100_INTERRUPT_REG_2_OFFSET);
	} else
		pr_warn("Unable to map board registers\n");

	pr_info("MVME7100 board from Artesyn\n");
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mvme7100_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "artesyn,MVME7100");
}

static void mvme7100_usb_host_fixup(struct pci_dev *pdev)
{
	unsigned int val;

	if (!machine_is(mvme7100))
		return;

	/* Ensure only ports 1 & 2 are enabled */
	pci_read_config_dword(pdev, 0xe0, &val);
	pci_write_config_dword(pdev, 0xe0, (val & ~7) | 0x2);

	/* System clock is 48-MHz Oscillator and EHCI Enabled. */
	pci_write_config_dword(pdev, 0xe4, 1 << 5);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_USB,
	mvme7100_usb_host_fixup);

machine_arch_initcall(mvme7100, mpc86xx_common_publish_devices);

define_machine(mvme7100) {
	.name			= "MVME7100",
	.probe			= mvme7100_probe,
	.setup_arch		= mvme7100_setup_arch,
	.init_IRQ		= mpc86xx_init_irq,
	.get_irq		= mpic_get_irq,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
};
