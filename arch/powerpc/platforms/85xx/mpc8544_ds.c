/*
 * MPC8544 DS Board Setup
 *
 * Author Xianghua Xiao (x.xiao@freescale.com)
 * Roy Zang <tie-fei.zang@freescale.com>
 * 	- Add PCI/PCI Exprees support
 * Copyright 2007 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/i8259.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include "mpc85xx.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define DBG(fmt, args...)
#endif

#ifdef CONFIG_PPC_I8259
static void mpc8544_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq != NO_IRQ) {
		generic_handle_irq(cascade_irq);
	}
	desc->chip->eoi(irq);
}
#endif	/* CONFIG_PPC_I8259 */

void __init mpc8544_ds_pic_init(void)
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
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);

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

	DBG("mpc8544ds: cascade mapped to irq %d\n", cascade_irq);

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	set_irq_chained_handler(cascade_irq, mpc8544_8259_cascade);
#endif	/* CONFIG_PPC_I8259 */
}

#ifdef CONFIG_PCI
enum pirq { PIRQA = 8, PIRQB, PIRQC, PIRQD, PIRQE, PIRQF, PIRQG, PIRQH };

/*
 * Value in  table -- IRQ number
 */
const unsigned char uli1575_irq_route_table[16] = {
	0,		/* 0: Reserved */
	0x8,
	0,		/* 2: Reserved */
	0x2,
	0x4,
	0x5,
	0x7,
	0x6,
	0,		/* 8: Reserved */
	0x1,
	0x3,
	0x9,
	0xb,
	0,		/* 13: Reserved */
	0xd,
	0xf,
};

static int __devinit
get_pci_irq_from_of(struct pci_controller *hose, int slot, int pin)
{
	struct of_irq oirq;
	u32 laddr[3];
	struct device_node *hosenode = hose ? hose->arch_data : NULL;

	if (!hosenode)
		return -EINVAL;

	laddr[0] = (hose->first_busno << 16) | (PCI_DEVFN(slot, 0) << 8);
	laddr[1] = laddr[2] = 0;
	of_irq_map_raw(hosenode, &pin, 1, laddr, &oirq);
	DBG("mpc8544_ds: pci irq addr %x, slot %d, pin %d, irq %d\n",
	    laddr[0], slot, pin, oirq.specifier[0]);
	return oirq.specifier[0];
}

/*8259*/
static void __devinit quirk_uli1575(struct pci_dev *dev)
{
	unsigned short temp;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	unsigned char irq2pin[16];
	unsigned long pirq_map_word = 0;
	u32 irq;
	int i;

	/*
	 * ULI1575 interrupts route setup
	 */
	memset(irq2pin, 0, 16);	/* Initialize default value 0 */

	irq2pin[6]=PIRQA+3;	/* enabled mapping for IRQ6 to PIRQD, used by SATA */

	/*
	 * PIRQE -> PIRQF mapping set manually
	 *
	 * IRQ pin   IRQ#
	 * PIRQE ---- 9
	 * PIRQF ---- 10
	 * PIRQG ---- 11
	 * PIRQH ---- 12
	 */
	for (i = 0; i < 4; i++)
		irq2pin[i + 9] = PIRQE + i;

	/* Set IRQ-PIRQ Mapping to ULI1575 */
	for (i = 0; i < 16; i++)
		if (irq2pin[i])
			pirq_map_word |= (uli1575_irq_route_table[i] & 0xf)
			    << ((irq2pin[i] - PIRQA) * 4);

	pirq_map_word |= 1<<26;	/* disable INTx in EP mode*/

	/* ULI1575 IRQ mapping conf register default value is 0xb9317542 */
	DBG("Setup ULI1575 IRQ mapping configuration register value = 0x%x\n",
		(int)pirq_map_word);
	pci_write_config_dword(dev, 0x48, pirq_map_word);

#define ULI1575_SET_DEV_IRQ(slot, pin, reg)				\
	do {								\
		int irq;						\
		irq = get_pci_irq_from_of(hose, slot, pin);		\
		if (irq > 0 && irq < 16) 				\
			pci_write_config_byte(dev, reg, irq2pin[irq]);	\
		else							\
			printk(KERN_WARNING "ULI1575 device"		\
				"(slot %d, pin %d) irq %d is invalid.\n", \
				slot, pin, irq);			\
	} while(0)

	/* USB 1.1 OHCI controller 1, slot 28, pin 1 */
	ULI1575_SET_DEV_IRQ(28, 1, 0x86);

	/* USB 1.1 OHCI controller 2, slot 28, pin 2 */
	ULI1575_SET_DEV_IRQ(28, 2, 0x87);

	/* USB 1.1 OHCI controller 3, slot 28, pin 3 */
	ULI1575_SET_DEV_IRQ(28, 3, 0x88);

	/* USB 2.0 controller, slot 28, pin 4 */
	irq = get_pci_irq_from_of(hose, 28, 4);
	if (irq >= 0 && irq <= 15)
		pci_write_config_dword(dev, 0x74, uli1575_irq_route_table[irq]);

	/* Audio controller, slot 29, pin 1 */
	ULI1575_SET_DEV_IRQ(29, 1, 0x8a);

	/* Modem controller, slot 29, pin 2 */
	ULI1575_SET_DEV_IRQ(29, 2, 0x8b);

	/* HD audio controller, slot 29, pin 3 */
	ULI1575_SET_DEV_IRQ(29, 3, 0x8c);

	/* SMB interrupt: slot 30, pin 1 */
	ULI1575_SET_DEV_IRQ(30, 1, 0x8e);

	/* PMU ACPI SCI interrupt: slot 30, pin 2 */
	ULI1575_SET_DEV_IRQ(30, 2, 0x8f);

	/* Serial ATA interrupt: slot 31, pin 1 */
	ULI1575_SET_DEV_IRQ(31, 1, 0x8d);

	/* Primary PATA IDE IRQ: 14
	 * Secondary PATA IDE IRQ: 15
	 */
	pci_write_config_byte(dev, 0x44, 0x30 | uli1575_irq_route_table[14]);
	pci_write_config_byte(dev, 0x75, uli1575_irq_route_table[15]);

	/* Set IRQ14 and IRQ15 to legacy IRQs */
	pci_read_config_word(dev, 0x46, &temp);
	temp |= 0xc000;
	pci_write_config_word(dev, 0x46, temp);

	/* Set i8259 interrupt trigger
	 * IRQ 3:  Level
	 * IRQ 4:  Level
	 * IRQ 5:  Level
	 * IRQ 6:  Level
	 * IRQ 7:  Level
	 * IRQ 9:  Level
	 * IRQ 10: Level
	 * IRQ 11: Level
	 * IRQ 12: Level
	 * IRQ 14: Edge
	 * IRQ 15: Edge
	 */
	outb(0xfa, 0x4d0);
	outb(0x1e, 0x4d1);

#undef ULI1575_SET_DEV_IRQ
}

/* SATA */
static void __devinit quirk_uli5288(struct pci_dev *dev)
{
	unsigned char c;

	pci_read_config_byte(dev, 0x83, &c);
	c |= 0x80;		/* read/write lock */
	pci_write_config_byte(dev, 0x83, c);

	pci_write_config_byte(dev, 0x09, 0x01);	/* Base class code: storage */
	pci_write_config_byte(dev, 0x0a, 0x06);	/* IDE disk */

	pci_read_config_byte(dev, 0x83, &c);
	c &= 0x7f;
	pci_write_config_byte(dev, 0x83, c);

	pci_read_config_byte(dev, 0x84, &c);
	c |= 0x01;				/* emulated PATA mode enabled */
	pci_write_config_byte(dev, 0x84, c);
}

/* PATA */
static void __devinit quirk_uli5229(struct pci_dev *dev)
{
	unsigned short temp;
	pci_write_config_word(dev, 0x04, 0x0405);	/* MEM IO MSI */
	pci_read_config_word(dev, 0x4a, &temp);
	temp |= 0x1000;				/* Enable Native IRQ 14/15 */
	pci_write_config_word(dev, 0x4a, temp);
}

/*Bridge*/
static void __devinit early_uli5249(struct pci_dev *dev)
{
	unsigned char temp;
	pci_write_config_word(dev, 0x04, 0x0007);	/* mem access */
	pci_read_config_byte(dev, 0x7c, &temp);
	pci_write_config_byte(dev, 0x7c, 0x80);	/* R/W lock control */
	pci_write_config_byte(dev, 0x09, 0x01);	/* set as pci-pci bridge */
	pci_write_config_byte(dev, 0x7c, temp);	/* restore pci bus debug control */
	dev->class |= 0x1;
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x1575, quirk_uli1575);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x5288, quirk_uli5288);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x5229, quirk_uli5229);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_AL, 0x5249, early_uli5249);
#endif	/* CONFIG_PCI */

/*
 * Setup the architecture
 */
static void __init mpc8544_ds_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc8544_ds_setup_arch()", 0);

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;) {
		struct resource rsrc;
		of_address_to_resource(np, 0, &rsrc);
		if ((rsrc.start & 0xfffff) == 0xb000)
			fsl_add_bridge(np, 1);
		else
			fsl_add_bridge(np, 0);
	}
#endif

	printk("MPC8544 DS board from Freescale Semiconductor\n");
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc8544_ds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "MPC8544DS");
}

define_machine(mpc8544_ds) {
	.name			= "MPC8544 DS",
	.probe			= mpc8544_ds_probe,
	.setup_arch		= mpc8544_ds_setup_arch,
	.init_IRQ		= mpc8544_ds_pic_init,
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.get_irq		= mpic_get_irq,
	.restart		= mpc85xx_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
