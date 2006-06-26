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

#include <linux/config.h>
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
#include <linux/root_dev.h>
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

/*
 * Internal interrupts are all Level Sensitive, and Positive Polarity
 *
 * Note:  Likely, this table and the following function should be
 *        obtained and derived from the OF Device Tree.
 */
static u_char mpc85xx_cds_openpic_initsenses[] __initdata = {
	MPC85XX_INTERNAL_IRQ_SENSES,
#if defined(CONFIG_PCI)
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* Ext 0: PCI slot 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext 1: PCI slot 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext 2: PCI slot 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* Ext 3: PCI slot 3 */
#else
	0x0,				/* External  0: */
	0x0,				/* External  1: */
	0x0,				/* External  2: */
	0x0,				/* External  3: */
#endif
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 5: PHY */
	0x0,				/* External  6: */
	0x0,				/* External  7: */
	0x0,				/* External  8: */
	0x0,				/* External  9: */
	0x0,				/* External 10: */
#ifdef CONFIG_PCI
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),    /* Ext 11: PCI2 slot 0 */
#else
	0x0,				/* External 11: */
#endif
};


#ifdef CONFIG_PCI
/*
 * interrupt routing
 */
int
mpc85xx_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);

	if (!hose->index)
	{
		/* Handle PCI1 interrupts */
		char pci_irq_table[][4] =
			/*
			 *      PCI IDSEL/INTPIN->INTLINE
			 *        A      B      C      D
			 */

			/* Note IRQ assignment for slots is based on which slot the elysium is
			 * in -- in this setup elysium is in slot #2 (this PIRQA as first
			 * interrupt on slot */
		{
			{ 0, 1, 2, 3 }, /* 16 - PMC */
			{ 0, 1, 2, 3 }, /* 17 P2P (Tsi320) */
			{ 0, 1, 2, 3 }, /* 18 - Slot 1 */
			{ 1, 2, 3, 0 }, /* 19 - Slot 2 */
			{ 2, 3, 0, 1 }, /* 20 - Slot 3 */
			{ 3, 0, 1, 2 }, /* 21 - Slot 4 */
		};

		const long min_idsel = 16, max_idsel = 21, irqs_per_slot = 4;
		int i, j;

		for (i = 0; i < 6; i++)
			for (j = 0; j < 4; j++)
				pci_irq_table[i][j] =
					((pci_irq_table[i][j] + 5 -
					  cds_pci_slot) & 0x3) + PIRQ0A;

		return PCI_IRQ_TABLE_LOOKUP;
	} else {
		/* Handle PCI2 interrupts (if we have one) */
		char pci_irq_table[][4] =
		{
			/*
			 * We only have one slot and one interrupt
			 * going to PIRQA - PIRQD */
			{ PIRQ1A, PIRQ1A, PIRQ1A, PIRQ1A }, /* 21 - slot 0 */
		};

		const long min_idsel = 21, max_idsel = 21, irqs_per_slot = 4;

		return PCI_IRQ_TABLE_LOOKUP;
	}
}

#define ARCADIA_HOST_BRIDGE_IDSEL	17
#define ARCADIA_2ND_BRIDGE_IDSEL	3

extern int mpc85xx_pci2_busno;

int
mpc85xx_exclude_device(u_char bus, u_char devfn)
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

void __init
mpc85xx_cds_pcibios_fixup(void)
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
}
#endif /* CONFIG_PCI */

void __init mpc85xx_cds_pic_init(void)
{
	struct mpic *mpic1;
	phys_addr_t OpenPIC_PAddr;

	/* Determine the Physical Address of the OpenPIC regs */
	OpenPIC_PAddr = get_immrbase() + MPC85xx_OPENPIC_OFFSET;

	mpic1 = mpic_alloc(OpenPIC_PAddr,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			4, MPC85xx_OPENPIC_IRQ_OFFSET, 0, 250,
			mpc85xx_cds_openpic_initsenses,
			sizeof(mpc85xx_cds_openpic_initsenses), " OpenPIC  ");
	BUG_ON(mpic1 == NULL);
	mpic_assign_isu(mpic1, 0, OpenPIC_PAddr + 0x10200);
	mpic_assign_isu(mpic1, 1, OpenPIC_PAddr + 0x10280);
	mpic_assign_isu(mpic1, 2, OpenPIC_PAddr + 0x10300);
	mpic_assign_isu(mpic1, 3, OpenPIC_PAddr + 0x10380);
	mpic_assign_isu(mpic1, 4, OpenPIC_PAddr + 0x10400);
	mpic_assign_isu(mpic1, 5, OpenPIC_PAddr + 0x10480);
	mpic_assign_isu(mpic1, 6, OpenPIC_PAddr + 0x10500);
	mpic_assign_isu(mpic1, 7, OpenPIC_PAddr + 0x10580);

	/* dummy mappings to get to 48 */
	mpic_assign_isu(mpic1, 8, OpenPIC_PAddr + 0x10600);
	mpic_assign_isu(mpic1, 9, OpenPIC_PAddr + 0x10680);
	mpic_assign_isu(mpic1, 10, OpenPIC_PAddr + 0x10700);
	mpic_assign_isu(mpic1, 11, OpenPIC_PAddr + 0x10780);

	/* External ints */
	mpic_assign_isu(mpic1, 12, OpenPIC_PAddr + 0x10000);
	mpic_assign_isu(mpic1, 13, OpenPIC_PAddr + 0x10080);
	mpic_assign_isu(mpic1, 14, OpenPIC_PAddr + 0x10100);

	mpic_init(mpic1);

#ifdef CONFIG_PCI
	mpic_setup_cascade(PIRQ0A, i8259_irq_cascade, NULL);

	i8259_init(0,0);
#endif
}


/*
 * Setup the architecture
 */
static void __init
mpc85xx_cds_setup_arch(void)
{
	struct device_node *cpu;
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_cds_setup_arch()", 0);

	cpu = of_find_node_by_type(NULL, "cpu");
	if (cpu != 0) {
		unsigned int *fp;

		fp = (int *)get_property(cpu, "clock-frequency", NULL);
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
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = mpc85xx_map_irq;
	ppc_md.pci_exclude_device = mpc85xx_exclude_device;
#endif

#ifdef  CONFIG_ROOT_NFS
	ROOT_DEV = Root_NFS;
#else
	ROOT_DEV = Root_HDA1;
#endif
}


void
mpc85xx_cds_show_cpuinfo(struct seq_file *m)
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
	/* We always match for now, eventually we should look at
	 * the flat dev tree to ensure this is the board we are
	 * supposed to run on
	 */
	return 1;
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
