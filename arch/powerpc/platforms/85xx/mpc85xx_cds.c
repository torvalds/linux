/*
 * MPC85xx setup and early boot code plus other random bits.
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/fsl_devices.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/irq.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/i8259.h>

#include <sysdev/fsl_soc.h>
#include "mpc85xx.h"

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif

static int cds_pci_slot = 2;
static volatile u8 *cadmus;

#ifdef CONFIG_PCI

#define ARCADIA_HOST_BRIDGE_IDSEL	17
#define ARCADIA_2ND_BRIDGE_IDSEL	3

extern int mpc85xx_pci2_busno;

static int mpc85xx_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (mpc85xx_pci2_busno)
		if (bus == (mpc85xx_pci2_busno) && PCI_SLOT(devfn) == 0)
			return PCIBIOS_DEVICE_NOT_FOUND;
	/* We explicitly do not go past the Tundra 320 Bridge */
	if ((bus == 1) && (PCI_SLOT(devfn) == ARCADIA_2ND_BRIDGE_IDSEL))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if ((bus == 0) && (PCI_SLOT(devfn) == ARCADIA_2ND_BRIDGE_IDSEL))
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

static void __init mpc85xx_cds_pcibios_fixup(void)
{
	struct pci_dev *dev;
	u_char		c;

	if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
					PCI_DEVICE_ID_VIA_82C586_1, NULL))) {
		/*
		 * U-Boot does not set the enable bits
		 * for the IDE device. Force them on here.
		 */
		pci_read_config_byte(dev, 0x40, &c);
		c |= 0x03; /* IDE: Chip Enable Bits */
		pci_write_config_byte(dev, 0x40, c);

		/*
		 * Since only primary interface works, force the
		 * IDE function to standard primary IDE interrupt
		 * w/ 8259 offset
		 */
		dev->irq = 14;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
		pci_dev_put(dev);
	}

	/*
	 * Force legacy USB interrupt routing
	 */
	if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
					PCI_DEVICE_ID_VIA_82C586_2, NULL))) {
		dev->irq = 10;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, 10);
		pci_dev_put(dev);
	}

	if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
					PCI_DEVICE_ID_VIA_82C586_2, dev))) {
		dev->irq = 11;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, 11);
		pci_dev_put(dev);
	}

	/* Now map all the PCI irqs */
	dev = NULL;
	for_each_pci_dev(dev)
		pci_read_irq_line(dev);
}

#ifdef CONFIG_PPC_I8259
#warning The i8259 PIC support is currently broken
static void mpc85xx_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);

	desc->chip->eoi(irq);
}
#endif /* PPC_I8259 */
#endif /* CONFIG_PCI */

static void __init mpc85xx_cds_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np = NULL;
#ifdef CONFIG_PPC_I8259
	struct device_node *cascade_node = NULL;
	int cascade_irq;
#endif

	np = of_find_node_by_type(np, "open-pic");

	if (np == NULL) {
		printk(KERN_ERR "Could not find open-pic node\n");
		return;
	}

	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "Failed to map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			4, 0, " OpenPIC  ");
	BUG_ON(mpic == NULL);

	/* Return the mpic node */
	of_node_put(np);

	mpic_assign_isu(mpic, 0, r.start + 0x10200);
	mpic_assign_isu(mpic, 1, r.start + 0x10280);
	mpic_assign_isu(mpic, 2, r.start + 0x10300);
	mpic_assign_isu(mpic, 3, r.start + 0x10380);
	mpic_assign_isu(mpic, 4, r.start + 0x10400);
	mpic_assign_isu(mpic, 5, r.start + 0x10480);
	mpic_assign_isu(mpic, 6, r.start + 0x10500);
	mpic_assign_isu(mpic, 7, r.start + 0x10580);

	/* Used only for 8548 so far, but no harm in
	 * allocating them for everyone */
	mpic_assign_isu(mpic, 8, r.start + 0x10600);
	mpic_assign_isu(mpic, 9, r.start + 0x10680);
	mpic_assign_isu(mpic, 10, r.start + 0x10700);
	mpic_assign_isu(mpic, 11, r.start + 0x10780);

	/* External Interrupts */
	mpic_assign_isu(mpic, 12, r.start + 0x10000);
	mpic_assign_isu(mpic, 13, r.start + 0x10080);
	mpic_assign_isu(mpic, 14, r.start + 0x10100);

	mpic_init(mpic);

#ifdef CONFIG_PPC_I8259
	/* Initialize the i8259 controller */
	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "chrp,iic")) {
			cascade_node = np;
			break;
		}

	if (cascade_node == NULL) {
		printk(KERN_DEBUG "Could not find i8259 PIC\n");
		return;
	}

	cascade_irq = irq_of_parse_and_map(cascade_node, 0);
	if (cascade_irq == NO_IRQ) {
		printk(KERN_ERR "Failed to map cascade interrupt\n");
		return;
	}

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	set_irq_chained_handler(cascade_irq, mpc85xx_8259_cascade);
#endif /* CONFIG_PPC_I8259 */
}

/*
 * Setup the architecture
 */
static void __init mpc85xx_cds_setup_arch(void)
{
	struct device_node *cpu;
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_cds_setup_arch()", 0);

	cpu = of_find_node_by_type(NULL, "cpu");
	if (cpu != 0) {
		const unsigned int *fp;

		fp = of_get_property(cpu, "clock-frequency", NULL);
		if (fp != 0)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 500000000 / HZ;
		of_node_put(cpu);
	}

	cadmus = ioremap(CADMUS_BASE, CADMUS_SIZE);
	cds_pci_slot = ((cadmus[CM_CSR] >> 6) & 0x3) + 1;

	if (ppc_md.progress) {
		char buf[40];
		snprintf(buf, 40, "CDS Version = 0x%x in slot %d\n",
				cadmus[CM_VER], cds_pci_slot);
		ppc_md.progress(buf, 0);
	}

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);

	ppc_md.pcibios_fixup = mpc85xx_cds_pcibios_fixup;
	ppc_md.pci_exclude_device = mpc85xx_exclude_device;
#endif
}

static void mpc85xx_cds_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");
	seq_printf(m, "Machine\t\t: MPC85xx CDS (0x%x)\n", cadmus[CM_VER]);
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}


/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc85xx_cds_probe(void)
{
        unsigned long root = of_get_flat_dt_root();

        return of_flat_dt_is_compatible(root, "MPC85xxCDS");
}

define_machine(mpc85xx_cds) {
	.name		= "MPC85xx CDS",
	.probe		= mpc85xx_cds_probe,
	.setup_arch	= mpc85xx_cds_setup_arch,
	.init_IRQ	= mpc85xx_cds_pic_init,
	.show_cpuinfo	= mpc85xx_cds_show_cpuinfo,
	.get_irq	= mpic_get_irq,
	.restart	= mpc85xx_restart,
	.calibrate_decr = generic_calibrate_decr,
	.progress	= udbg_progress,
};
