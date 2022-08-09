// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GE IMP3A Board Setup
 *
 * Author Martyn Welch <martyn.welch@ge.com>
 *
 * Copyright 2010 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on: mpc85xx_ds.c (MPC85xx DS Board Setup)
 * Copyright 2007 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/swiotlb.h>
#include <asm/nvram.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include "smp.h"

#include "mpc85xx.h"
#include <sysdev/ge/ge_pic.h>

void __iomem *imp3a_regs;

void __init ge_imp3a_pic_init(void)
{
	struct mpic *mpic;
	struct device_node *np;
	struct device_node *cascade_node = NULL;

	if (of_machine_is_compatible("fsl,MPC8572DS-CAMP")) {
		mpic = mpic_alloc(NULL, 0,
			MPIC_NO_RESET |
			MPIC_BIG_ENDIAN |
			MPIC_SINGLE_DEST_CPU,
			0, 256, " OpenPIC  ");
	} else {
		mpic = mpic_alloc(NULL, 0,
			  MPIC_BIG_ENDIAN |
			  MPIC_SINGLE_DEST_CPU,
			0, 256, " OpenPIC  ");
	}

	BUG_ON(mpic == NULL);
	mpic_init(mpic);
	/*
	 * There is a simple interrupt handler in the main FPGA, this needs
	 * to be cascaded into the MPIC
	 */
	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "gef,fpga-pic-1.00")) {
			cascade_node = np;
			break;
		}

	if (cascade_node == NULL) {
		printk(KERN_WARNING "IMP3A: No FPGA PIC\n");
		return;
	}

	gef_pic_init(cascade_node);
	of_node_put(cascade_node);
}

static void __init ge_imp3a_pci_assign_primary(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
	struct resource rsrc;

	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8540-pci") ||
		    of_device_is_compatible(np, "fsl,mpc8548-pcie") ||
		    of_device_is_compatible(np, "fsl,p2020-pcie")) {
			of_address_to_resource(np, 0, &rsrc);
			if ((rsrc.start & 0xfffff) == 0x9000)
				fsl_pci_primary = np;
		}
	}
#endif
}

/*
 * Setup the architecture
 */
static void __init ge_imp3a_setup_arch(void)
{
	struct device_node *regs;

	if (ppc_md.progress)
		ppc_md.progress("ge_imp3a_setup_arch()", 0);

	mpc85xx_smp_init();

	ge_imp3a_pci_assign_primary();

	swiotlb_detect_4g();

	/* Remap basic board registers */
	regs = of_find_compatible_node(NULL, NULL, "ge,imp3a-fpga-regs");
	if (regs) {
		imp3a_regs = of_iomap(regs, 0);
		if (imp3a_regs == NULL)
			printk(KERN_WARNING "Unable to map board registers\n");
		of_node_put(regs);
	}

#if defined(CONFIG_MMIO_NVRAM)
	mmio_nvram_init();
#endif

	printk(KERN_INFO "GE Intelligent Platforms IMP3A 3U cPCI SBC\n");
}

/* Return the PCB revision */
static unsigned int ge_imp3a_get_pcb_rev(void)
{
	unsigned int reg;

	reg = ioread16(imp3a_regs);
	return (reg >> 8) & 0xff;
}

/* Return the board (software) revision */
static unsigned int ge_imp3a_get_board_rev(void)
{
	unsigned int reg;

	reg = ioread16(imp3a_regs + 0x2);
	return reg & 0xff;
}

/* Return the FPGA revision */
static unsigned int ge_imp3a_get_fpga_rev(void)
{
	unsigned int reg;

	reg = ioread16(imp3a_regs + 0x2);
	return (reg >> 8) & 0xff;
}

/* Return compactPCI Geographical Address */
static unsigned int ge_imp3a_get_cpci_geo_addr(void)
{
	unsigned int reg;

	reg = ioread16(imp3a_regs + 0x6);
	return (reg & 0x0f00) >> 8;
}

/* Return compactPCI System Controller Status */
static unsigned int ge_imp3a_get_cpci_is_syscon(void)
{
	unsigned int reg;

	reg = ioread16(imp3a_regs + 0x6);
	return reg & (1 << 12);
}

static void ge_imp3a_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "Vendor\t\t: GE Intelligent Platforms\n");

	seq_printf(m, "Revision\t: %u%c\n", ge_imp3a_get_pcb_rev(),
		('A' + ge_imp3a_get_board_rev() - 1));

	seq_printf(m, "FPGA Revision\t: %u\n", ge_imp3a_get_fpga_rev());

	seq_printf(m, "cPCI geo. addr\t: %u\n", ge_imp3a_get_cpci_geo_addr());

	seq_printf(m, "cPCI syscon\t: %s\n",
		ge_imp3a_get_cpci_is_syscon() ? "yes" : "no");
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init ge_imp3a_probe(void)
{
	return of_machine_is_compatible("ge,IMP3A");
}

machine_arch_initcall(ge_imp3a, mpc85xx_common_publish_devices);

define_machine(ge_imp3a) {
	.name			= "GE_IMP3A",
	.probe			= ge_imp3a_probe,
	.setup_arch		= ge_imp3a_setup_arch,
	.init_IRQ		= ge_imp3a_pic_init,
	.show_cpuinfo		= ge_imp3a_show_cpuinfo,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
