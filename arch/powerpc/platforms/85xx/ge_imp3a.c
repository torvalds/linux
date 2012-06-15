/*
 * GE IMP3A Board Setup
 *
 * Author Martyn Welch <martyn.welch@ge.com>
 *
 * Copyright 2010 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
#include <linux/memblock.h>

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
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "fsl,MPC8572DS-CAMP")) {
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

#ifdef CONFIG_PCI
static int primary_phb_addr;
#endif	/* CONFIG_PCI */

/*
 * Setup the architecture
 */
static void __init ge_imp3a_setup_arch(void)
{
	struct device_node *regs;
#ifdef CONFIG_PCI
	struct device_node *np;
	struct pci_controller *hose;
#endif
	dma_addr_t max = 0xffffffff;

	if (ppc_md.progress)
		ppc_md.progress("ge_imp3a_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8540-pci") ||
		    of_device_is_compatible(np, "fsl,mpc8548-pcie") ||
		    of_device_is_compatible(np, "fsl,p2020-pcie")) {
			struct resource rsrc;
			of_address_to_resource(np, 0, &rsrc);
			if ((rsrc.start & 0xfffff) == primary_phb_addr)
				fsl_add_bridge(np, 1);
			else
				fsl_add_bridge(np, 0);

			hose = pci_find_hose_for_OF_device(np);
			max = min(max, hose->dma_window_base_cur +
					hose->dma_window_size);
		}
	}
#endif

	mpc85xx_smp_init();

#ifdef CONFIG_SWIOTLB
	if (memblock_end_of_DRAM() > max) {
		ppc_swiotlb_enable = 1;
		set_pci_dma_ops(&swiotlb_dma_ops);
		ppc_md.pci_dma_dev_setup = pci_dma_dev_setup_swiotlb;
	}
#endif

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
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "ge,IMP3A")) {
#ifdef CONFIG_PCI
		primary_phb_addr = 0x9000;
#endif
		return 1;
	}

	return 0;
}

machine_device_initcall(ge_imp3a, mpc85xx_common_publish_devices);

machine_arch_initcall(ge_imp3a, swiotlb_setup_bus_notifier);

define_machine(ge_imp3a) {
	.name			= "GE_IMP3A",
	.probe			= ge_imp3a_probe,
	.setup_arch		= ge_imp3a_setup_arch,
	.init_IRQ		= ge_imp3a_pic_init,
	.show_cpuinfo		= ge_imp3a_show_cpuinfo,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
