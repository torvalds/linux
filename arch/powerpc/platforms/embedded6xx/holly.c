// SPDX-License-Identifier: GPL-2.0-only
/*
 * Board setup routines for the IBM 750GX/CL platform w/ TSI10x bridge
 *
 * Copyright 2007 IBM Corporation
 *
 * Stephen Winiecki <stevewin@us.ibm.com>
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * Based on code from mpc7448_hpc2.c
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <linux/of_platform.h>
#include <linux/extable.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/tsi108.h>
#include <asm/pci-bridge.h>
#include <asm/reg.h>
#include <mm/mmu_decl.h>
#include <asm/tsi108_irq.h>
#include <asm/tsi108_pci.h>
#include <asm/mpic.h>

#undef DEBUG

#define HOLLY_PCI_CFG_PHYS 0x7c000000

static int holly_exclude_device(struct pci_controller *hose, u_char bus,
				u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

static void holly_remap_bridge(void)
{
	u32 lut_val, lut_addr;
	int i;

	printk(KERN_INFO "Remapping PCI bridge\n");

	/* Re-init the PCI bridge and LUT registers to have mappings that don't
	 * rely on PIBS
	 */
	lut_addr = 0x900;
	for (i = 0; i < 31; i++) {
		tsi108_write_reg(TSI108_PB_OFFSET + lut_addr, 0x00000201);
		lut_addr += 4;
		tsi108_write_reg(TSI108_PB_OFFSET + lut_addr, 0x0);
		lut_addr += 4;
	}

	/* Reserve the last LUT entry for PCI I/O space */
	tsi108_write_reg(TSI108_PB_OFFSET + lut_addr, 0x00000241);
	lut_addr += 4;
	tsi108_write_reg(TSI108_PB_OFFSET + lut_addr, 0x0);

	/* Map PCI I/O space */
	tsi108_write_reg(TSI108_PCI_PFAB_IO_UPPER, 0x0);
	tsi108_write_reg(TSI108_PCI_PFAB_IO, 0x1);

	/* Map PCI CFG space */
	tsi108_write_reg(TSI108_PCI_PFAB_BAR0_UPPER, 0x0);
	tsi108_write_reg(TSI108_PCI_PFAB_BAR0, 0x7c000000 | 0x01);

	/* We don't need MEM32 and PRM remapping so disable them */
	tsi108_write_reg(TSI108_PCI_PFAB_MEM32, 0x0);
	tsi108_write_reg(TSI108_PCI_PFAB_PFM3, 0x0);
	tsi108_write_reg(TSI108_PCI_PFAB_PFM4, 0x0);

	/* Set P2O_BAR0 */
	tsi108_write_reg(TSI108_PCI_P2O_BAR0_UPPER, 0x0);
	tsi108_write_reg(TSI108_PCI_P2O_BAR0, 0xc0000000);

	/* Init the PCI LUTs to do no remapping */
	lut_addr = 0x500;
	lut_val = 0x00000002;

	for (i = 0; i < 32; i++) {
		tsi108_write_reg(TSI108_PCI_OFFSET + lut_addr, lut_val);
		lut_addr += 4;
		tsi108_write_reg(TSI108_PCI_OFFSET + lut_addr, 0x40000000);
		lut_addr += 4;
		lut_val += 0x02000000;
	}
	tsi108_write_reg(TSI108_PCI_P2O_PAGE_SIZES, 0x00007900);

	/* Set 64-bit PCI bus address for system memory */
	tsi108_write_reg(TSI108_PCI_P2O_BAR2_UPPER, 0x0);
	tsi108_write_reg(TSI108_PCI_P2O_BAR2, 0x0);
}

static void __init holly_init_pci(void)
{
	struct device_node *np;

	if (ppc_md.progress)
		ppc_md.progress("holly_setup_arch():set_bridge", 0);

	/* setup PCI host bridge */
	holly_remap_bridge();

	np = of_find_node_by_type(NULL, "pci");
	if (np)
		tsi108_setup_pci(np, HOLLY_PCI_CFG_PHYS, 1);

	ppc_md.pci_exclude_device = holly_exclude_device;
	if (ppc_md.progress)
		ppc_md.progress("tsi108: resources set", 0x100);
}

static void __init holly_setup_arch(void)
{
	tsi108_csr_vir_base = get_vir_csrbase();

	printk(KERN_INFO "PPC750GX/CL Platform\n");
}

/*
 * Interrupt setup and service.  Interrupts on the holly come
 * from the four external INT pins, PCI interrupts are routed via
 * PCI interrupt control registers, it generates internal IRQ23
 *
 * Interrupt routing on the Holly Board:
 * TSI108:PB_INT[0] -> CPU0:INT#
 * TSI108:PB_INT[1] -> CPU0:MCP#
 * TSI108:PB_INT[2] -> N/C
 * TSI108:PB_INT[3] -> N/C
 */
static void __init holly_init_IRQ(void)
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
		printk(KERN_ERR "%s: No tsi108 pci node found !\n", __func__);
		return;
	}

	cascade_node = of_find_node_by_type(NULL, "pic-router");
	if (cascade_node == NULL) {
		printk(KERN_ERR "%s: No tsi108 pci cascade node found !\n", __func__);
		return;
	}

	cascade_pci_irq = irq_of_parse_and_map(tsi_pci, 0);
	pr_debug("%s: tsi108 cascade_pci_irq = 0x%x\n", __func__, (u32) cascade_pci_irq);
	tsi108_pci_int_init(cascade_node);
	irq_set_handler_data(cascade_pci_irq, mpic);
	irq_set_chained_handler(cascade_pci_irq, tsi108_irq_cascade);
#endif
	/* Configure MPIC outputs to CPU0 */
	tsi108_write_reg(TSI108_MPIC_OFFSET + 0x30c, 0);
}

static void holly_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: IBM\n");
	seq_printf(m, "machine\t\t: PPC750 GX/CL\n");
}

static void __noreturn holly_restart(char *cmd)
{
	__be32 __iomem *ocn_bar1 = NULL;
	unsigned long bar;
	struct device_node *bridge = NULL;
	const void *prop;
	int size;
	phys_addr_t addr = 0xc0000000;

	local_irq_disable();

	bridge = of_find_node_by_type(NULL, "tsi-bridge");
	if (bridge) {
		prop = of_get_property(bridge, "reg", &size);
		addr = of_translate_address(bridge, prop);
	}
	addr += (TSI108_PB_OFFSET + 0x414);

	ocn_bar1 = ioremap(addr, 0x4);

	/* Turn on the BOOT bit so the addresses are correctly
	 * routed to the HLP interface */
	bar = ioread32be(ocn_bar1);
	bar |= 2;
	iowrite32be(bar, ocn_bar1);
	iosync();

	/* Set SRR0 to the reset vector and turn on MSR_IP */
	mtspr(SPRN_SRR0, 0xfff00100);
	mtspr(SPRN_SRR1, MSR_IP);

	/* Do an rfi to jump back to firmware.  Somewhat evil,
	 * but it works
	 */
	__asm__ __volatile__("rfi" : : : "memory");

	/* Spin until reset happens.  Shouldn't really get here */
	for (;;) ;
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init holly_probe(void)
{
	if (!of_machine_is_compatible("ibm,holly"))
		return 0;
	return 1;
}

static int ppc750_machine_check_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *entry;

	/* Are we prepared to handle this fault */
	if ((entry = search_exception_tables(regs->nip)) != NULL) {
		tsi108_clear_pci_cfg_error();
		regs_set_return_msr(regs, regs->msr | MSR_RI);
		regs_set_return_ip(regs, extable_fixup(entry));
		return 1;
	}
	return 0;
}

define_machine(holly){
	.name                   	= "PPC750 GX/CL TSI",
	.probe                  	= holly_probe,
	.setup_arch             	= holly_setup_arch,
	.discover_phbs			= holly_init_pci,
	.init_IRQ               	= holly_init_IRQ,
	.show_cpuinfo           	= holly_show_cpuinfo,
	.get_irq                	= mpic_get_irq,
	.restart                	= holly_restart,
	.calibrate_decr         	= generic_calibrate_decr,
	.machine_check_exception	= ppc750_machine_check_exception,
	.progress               	= udbg_progress,
};
