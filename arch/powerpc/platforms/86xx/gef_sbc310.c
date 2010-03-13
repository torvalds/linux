/*
 * GE SBC310 board support
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 *
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Based on: mpc86xx_hpcn.c (MPC86xx HPCN board specific routines)
 * Copyright 2006 Freescale Semiconductor Inc.
 *
 * NEC fixup adapted from arch/mips/pci/fixup-lm2e.c
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <asm/mpic.h>
#include <asm/nvram.h>

#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>

#include "mpc86xx.h"
#include "gef_pic.h"

#undef DEBUG

#ifdef DEBUG
#define DBG (fmt...) do { printk(KERN_ERR "SBC310: " fmt); } while (0)
#else
#define DBG (fmt...) do { } while (0)
#endif

void __iomem *sbc310_regs;

static void __init gef_sbc310_init_irq(void)
{
	struct device_node *cascade_node = NULL;

	mpc86xx_init_irq();

	/*
	 * There is a simple interrupt handler in the main FPGA, this needs
	 * to be cascaded into the MPIC
	 */
	cascade_node = of_find_compatible_node(NULL, NULL, "gef,fpga-pic");
	if (!cascade_node) {
		printk(KERN_WARNING "SBC310: No FPGA PIC\n");
		return;
	}

	gef_pic_init(cascade_node);
	of_node_put(cascade_node);
}

static void __init gef_sbc310_setup_arch(void)
{
	struct device_node *regs;
#ifdef CONFIG_PCI
	struct device_node *np;

	for_each_compatible_node(np, "pci", "fsl,mpc8641-pcie") {
		fsl_add_bridge(np, 1);
	}
#endif

	printk(KERN_INFO "GE Intelligent Platforms SBC310 6U VPX SBC\n");

#ifdef CONFIG_SMP
	mpc86xx_smp_init();
#endif

	/* Remap basic board registers */
	regs = of_find_compatible_node(NULL, NULL, "gef,fpga-regs");
	if (regs) {
		sbc310_regs = of_iomap(regs, 0);
		if (sbc310_regs == NULL)
			printk(KERN_WARNING "Unable to map board registers\n");
		of_node_put(regs);
	}

#if defined(CONFIG_MMIO_NVRAM)
	mmio_nvram_init();
#endif
}

/* Return the PCB revision */
static unsigned int gef_sbc310_get_board_id(void)
{
	unsigned int reg;

	reg = ioread32(sbc310_regs);
	return reg & 0xff;
}

/* Return the PCB revision */
static unsigned int gef_sbc310_get_pcb_rev(void)
{
	unsigned int reg;

	reg = ioread32(sbc310_regs);
	return (reg >> 8) & 0xff;
}

/* Return the board (software) revision */
static unsigned int gef_sbc310_get_board_rev(void)
{
	unsigned int reg;

	reg = ioread32(sbc310_regs);
	return (reg >> 16) & 0xff;
}

/* Return the FPGA revision */
static unsigned int gef_sbc310_get_fpga_rev(void)
{
	unsigned int reg;

	reg = ioread32(sbc310_regs);
	return (reg >> 24) & 0xf;
}

static void gef_sbc310_show_cpuinfo(struct seq_file *m)
{
	uint svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: GE Intelligent Platforms\n");

	seq_printf(m, "Board ID\t: 0x%2.2x\n", gef_sbc310_get_board_id());
	seq_printf(m, "Revision\t: %u%c\n", gef_sbc310_get_pcb_rev(),
		('A' + gef_sbc310_get_board_rev() - 1));
	seq_printf(m, "FPGA Revision\t: %u\n", gef_sbc310_get_fpga_rev());

	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

}

static void __init gef_sbc310_nec_fixup(struct pci_dev *pdev)
{
	unsigned int val;

	/* Do not do the fixup on other platforms! */
	if (!machine_is(gef_sbc310))
		return;

	printk(KERN_INFO "Running NEC uPD720101 Fixup\n");

	/* Ensure only ports 1 & 2 are enabled */
	pci_read_config_dword(pdev, 0xe0, &val);
	pci_write_config_dword(pdev, 0xe0, (val & ~7) | 0x2);

	/* System clock is 48-MHz Oscillator and EHCI Enabled. */
	pci_write_config_dword(pdev, 0xe4, 1 << 5);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_USB,
	gef_sbc310_nec_fixup);

/*
 * Called very early, device-tree isn't unflattened
 *
 * This function is called to determine whether the BSP is compatible with the
 * supplied device-tree, which is assumed to be the correct one for the actual
 * board. It is expected thati, in the future, a kernel may support multiple
 * boards.
 */
static int __init gef_sbc310_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "gef,sbc310"))
		return 1;

	return 0;
}

static long __init mpc86xx_time_init(void)
{
	unsigned int temp;

	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	temp = mfspr(SPRN_HID0);
	temp |= HID0_TBEN;
	mtspr(SPRN_HID0, temp);
	asm volatile("isync");

	return 0;
}

static __initdata struct of_device_id of_bus_ids[] = {
	{ .compatible = "simple-bus", },
	{ .compatible = "gianfar", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	printk(KERN_DEBUG "Probe platform devices\n");
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(gef_sbc310, declare_of_platform_devices);

define_machine(gef_sbc310) {
	.name			= "GE SBC310",
	.probe			= gef_sbc310_probe,
	.setup_arch		= gef_sbc310_setup_arch,
	.init_IRQ		= gef_sbc310_init_irq,
	.show_cpuinfo		= gef_sbc310_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
};
