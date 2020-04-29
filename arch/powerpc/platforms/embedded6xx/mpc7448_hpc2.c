// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mpc7448_hpc2.c
 *
 * Board setup routines for the Freescale mpc7448hpc2(taiga) platform
 *
 * Author: Jacob Pan
 *	 jacob.pan@freescale.com
 * Author: Xianghua Xiao
 *       x.xiao@freescale.com
 * Maintainer: Roy Zang <tie-fei.zang@freescale.com>
 * 	Add Flat Device Tree support fot mpc7448hpc2 board
 *
 * Copyright 2004-2006 Freescale Semiconductor, Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/extable.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/tsi108.h>
#include <asm/pci-bridge.h>
#include <asm/reg.h>
#include <mm/mmu_decl.h>
#include <asm/tsi108_pci.h>
#include <asm/tsi108_irq.h>
#include <asm/mpic.h>

#undef DEBUG
#ifdef DEBUG
#define DBG(fmt...) do { printk(fmt); } while(0)
#else
#define DBG(fmt...) do { } while(0)
#endif

#define MPC7448HPC2_PCI_CFG_PHYS 0xfb000000

int mpc7448_hpc2_exclude_device(struct pci_controller *hose,
				u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

static void __init mpc7448_hpc2_setup_arch(void)
{
	struct device_node *np;
	if (ppc_md.progress)
		ppc_md.progress("mpc7448_hpc2_setup_arch():set_bridge", 0);

	tsi108_csr_vir_base = get_vir_csrbase();

	/* setup PCI host bridge */
#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "tsi108-pci")
		tsi108_setup_pci(np, MPC7448HPC2_PCI_CFG_PHYS, 0);

	ppc_md.pci_exclude_device = mpc7448_hpc2_exclude_device;
	if (ppc_md.progress)
		ppc_md.progress("tsi108: resources set", 0x100);
#endif

	printk(KERN_INFO "MPC7448HPC2 (TAIGA) Platform\n");
	printk(KERN_INFO
	       "Jointly ported by Freescale and Tundra Semiconductor\n");
	printk(KERN_INFO
	       "Enabling L2 cache then enabling the HID0 prefetch engine.\n");
}

/*
 * Interrupt setup and service.  Interrupts on the mpc7448_hpc2 come
 * from the four external INT pins, PCI interrupts are routed via
 * PCI interrupt control registers, it generates internal IRQ23
 *
 * Interrupt routing on the Taiga Board:
 * TSI108:PB_INT[0] -> CPU0:INT#
 * TSI108:PB_INT[1] -> CPU0:MCP#
 * TSI108:PB_INT[2] -> N/C
 * TSI108:PB_INT[3] -> N/C
 */
static void __init mpc7448_hpc2_init_IRQ(void)
{
	struct mpic *mpic;
#ifdef CONFIG_PCI
	unsigned int cascade_pci_irq;
	struct device_node *tsi_pci;
	struct device_node *cascade_node = NULL;
#endif

	mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
			MPIC_SPV_EOI | MPIC_NO_PTHROU_DIS | MPIC_REGSET_TSI108,
			24, 0,
			"Tsi108_PIC");

	BUG_ON(mpic == NULL);

	mpic_assign_isu(mpic, 0, mpic->paddr + 0x100);

	mpic_init(mpic);

#ifdef CONFIG_PCI
	tsi_pci = of_find_node_by_type(NULL, "pci");
	if (tsi_pci == NULL) {
		printk("%s: No tsi108 pci node found !\n", __func__);
		return;
	}
	cascade_node = of_find_node_by_type(NULL, "pic-router");
	if (cascade_node == NULL) {
		printk("%s: No tsi108 pci cascade node found !\n", __func__);
		return;
	}

	cascade_pci_irq = irq_of_parse_and_map(tsi_pci, 0);
	DBG("%s: tsi108 cascade_pci_irq = 0x%x\n", __func__,
	    (u32) cascade_pci_irq);
	tsi108_pci_int_init(cascade_node);
	irq_set_handler_data(cascade_pci_irq, mpic);
	irq_set_chained_handler(cascade_pci_irq, tsi108_irq_cascade);
#endif
	/* Configure MPIC outputs to CPU0 */
	tsi108_write_reg(TSI108_MPIC_OFFSET + 0x30c, 0);
}

void mpc7448_hpc2_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Freescale Semiconductor\n");
}

static void __noreturn mpc7448_hpc2_restart(char *cmd)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	for (;;) ;		/* Spin until reset happens */
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc7448_hpc2_probe(void)
{
	if (!of_machine_is_compatible("mpc74xx"))
		return 0;
	return 1;
}

static int mpc7448_machine_check_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *entry;

	/* Are we prepared to handle this fault */
	if ((entry = search_exception_tables(regs->nip)) != NULL) {
		tsi108_clear_pci_cfg_error();
		regs->msr |= MSR_RI;
		regs->nip = extable_fixup(entry);
		return 1;
	}
	return 0;
}

define_machine(mpc7448_hpc2){
	.name 			= "MPC7448 HPC2",
	.probe 			= mpc7448_hpc2_probe,
	.setup_arch 		= mpc7448_hpc2_setup_arch,
	.init_IRQ 		= mpc7448_hpc2_init_IRQ,
	.show_cpuinfo 		= mpc7448_hpc2_show_cpuinfo,
	.get_irq 		= mpic_get_irq,
	.restart 		= mpc7448_hpc2_restart,
	.calibrate_decr 	= generic_calibrate_decr,
	.machine_check_exception= mpc7448_machine_check_exception,
	.progress 		= udbg_progress,
};
