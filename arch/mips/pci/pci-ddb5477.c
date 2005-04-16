/*
 * PCI code for DDB5477.
 *
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/bootinfo.h>
#include <asm/debug.h>

#include <asm/ddb5xxx/ddb5xxx.h>

static struct resource extpci_io_resource = {
	"ext pci IO space",
	DDB_PCI0_IO_BASE - DDB_PCI_IO_BASE + 0x4000,
	DDB_PCI0_IO_BASE - DDB_PCI_IO_BASE + DDB_PCI0_IO_SIZE - 1,
	IORESOURCE_IO
};

static struct resource extpci_mem_resource = {
	"ext pci memory space",
	DDB_PCI0_MEM_BASE + 0x100000,
	DDB_PCI0_MEM_BASE + DDB_PCI0_MEM_SIZE - 1,
	IORESOURCE_MEM
};

static struct resource iopci_io_resource = {
	"io pci IO space",
	DDB_PCI1_IO_BASE - DDB_PCI_IO_BASE,
	DDB_PCI1_IO_BASE - DDB_PCI_IO_BASE + DDB_PCI1_IO_SIZE - 1,
	IORESOURCE_IO
};

static struct resource iopci_mem_resource = {
	"ext pci memory space",
	DDB_PCI1_MEM_BASE,
	DDB_PCI1_MEM_BASE + DDB_PCI1_MEM_SIZE - 1,
	IORESOURCE_MEM
};

extern struct pci_ops ddb5477_ext_pci_ops;
extern struct pci_ops ddb5477_io_pci_ops;

struct pci_controller ddb5477_ext_controller = {
	.pci_ops	= &ddb5477_ext_pci_ops,
	.io_resource	= &extpci_io_resource,
	.mem_resource	= &extpci_mem_resource
};

struct pci_controller ddb5477_io_controller = {
	.pci_ops	= &ddb5477_io_pci_ops,
	.io_resource	= &iopci_io_resource,
	.mem_resource	= &iopci_mem_resource
};



/*
 * we fix up irqs based on the slot number.
 * The first entry is at AD:11.
 * Fortunately this works because, although we have two pci buses,
 * they all have different slot numbers (except for rockhopper slot 20
 * which is handled below).
 *
 */

/*
 * irq mapping : device -> pci int # -> vrc4377 irq# , 
 * ddb5477 board manual page 4  and vrc5477 manual page 46
 */

/*
 * based on ddb5477 manual page 11
 */
#define		MAX_SLOT_NUM		21
static unsigned char irq_map[MAX_SLOT_NUM] = {
	/* SLOT:  0, AD:11 */ 0xff,
	/* SLOT:  1, AD:12 */ 0xff,
	/* SLOT:  2, AD:13 */ 0xff,
	/* SLOT:  3, AD:14 */ 0xff,
	/* SLOT:  4, AD:15 */ VRC5477_IRQ_INTA, /* onboard tulip */
	/* SLOT:  5, AD:16 */ VRC5477_IRQ_INTB, /* slot 1 */
	/* SLOT:  6, AD:17 */ VRC5477_IRQ_INTC, /* slot 2 */
	/* SLOT:  7, AD:18 */ VRC5477_IRQ_INTD, /* slot 3 */
	/* SLOT:  8, AD:19 */ VRC5477_IRQ_INTE, /* slot 4 */
	/* SLOT:  9, AD:20 */ 0xff,
	/* SLOT: 10, AD:21 */ 0xff,
	/* SLOT: 11, AD:22 */ 0xff,
	/* SLOT: 12, AD:23 */ 0xff,
	/* SLOT: 13, AD:24 */ 0xff,
	/* SLOT: 14, AD:25 */ 0xff,
	/* SLOT: 15, AD:26 */ 0xff,
	/* SLOT: 16, AD:27 */ 0xff,
	/* SLOT: 17, AD:28 */ 0xff,
	/* SLOT: 18, AD:29 */ VRC5477_IRQ_IOPCI_INTC, /* vrc5477 ac97 */
	/* SLOT: 19, AD:30 */ VRC5477_IRQ_IOPCI_INTB, /* vrc5477 usb peri */
	/* SLOT: 20, AD:31 */ VRC5477_IRQ_IOPCI_INTA, /* vrc5477 usb host */
};
static unsigned char rockhopperII_irq_map[MAX_SLOT_NUM] = {
	/* SLOT:  0, AD:11 */ 0xff,
	/* SLOT:  1, AD:12 */ VRC5477_IRQ_INTB, /* onboard AMD PCNET */
	/* SLOT:  2, AD:13 */ 0xff,
	/* SLOT:  3, AD:14 */ 0xff,
	/* SLOT:  4, AD:15 */ 14, /* M5229 ide ISA irq */
	/* SLOT:  5, AD:16 */ VRC5477_IRQ_INTD, /* slot 3 */
	/* SLOT:  6, AD:17 */ VRC5477_IRQ_INTA, /* slot 4 */
	/* SLOT:  7, AD:18 */ VRC5477_IRQ_INTD, /* slot 5 */
	/* SLOT:  8, AD:19 */ 0, /* M5457 modem nop */
	/* SLOT:  9, AD:20 */ VRC5477_IRQ_INTA, /* slot 2 */
	/* SLOT: 10, AD:21 */ 0xff,
	/* SLOT: 11, AD:22 */ 0xff,
	/* SLOT: 12, AD:23 */ 0xff,
	/* SLOT: 13, AD:24 */ 0xff,
	/* SLOT: 14, AD:25 */ 0xff,
	/* SLOT: 15, AD:26 */ 0xff,
	/* SLOT: 16, AD:27 */ 0xff,
	/* SLOT: 17, AD:28 */ 0, /* M7101 PMU nop */
	/* SLOT: 18, AD:29 */ VRC5477_IRQ_IOPCI_INTC, /* vrc5477 ac97 */
	/* SLOT: 19, AD:30 */ VRC5477_IRQ_IOPCI_INTB, /* vrc5477 usb peri */
	/* SLOT: 20, AD:31 */ VRC5477_IRQ_IOPCI_INTA, /* vrc5477 usb host */
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int slot_num;
	unsigned char *slot_irq_map;
	unsigned char irq;

	/* 
	 * We ignore the swizzled slot and pin values.  The original
	 * pci_fixup_irq() codes largely base irq number on the dev slot 
	 * numbers because except for one case they are unique even
	 * though there are multiple pci buses.
	 */

	if (mips_machtype == MACH_NEC_ROCKHOPPERII)
		slot_irq_map = rockhopperII_irq_map;
	else
		slot_irq_map = irq_map;

	slot_num = PCI_SLOT(dev->devfn);
	irq = slot_irq_map[slot_num];

	db_assert(slot_num < MAX_SLOT_NUM);

	db_assert(irq != 0xff);

	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	if (mips_machtype == MACH_NEC_ROCKHOPPERII) {
		/* hack to distinquish overlapping slot 20s, one
		 * on bus 0 (ALI USB on the M1535 on the backplane), 
		 * and one on bus 2 (NEC USB controller on the CPU board)
		 * Make the M1535 USB - ISA IRQ number 9.
		 */
		if (slot_num == 20 && dev->bus->number == 0) {
			pci_write_config_byte(dev,
					      PCI_INTERRUPT_LINE,
					      9);
			irq = 9;
		}

	}

	return irq;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

void ddb_pci_reset_bus(void)
{
	u32 temp;

	/*
	 * I am not sure about the "official" procedure, the following
	 * steps work as far as I know:
	 * We first set PCI cold reset bit (bit 31) in PCICTRL-H.
	 * Then we clear the PCI warm reset bit (bit 30) to 0 in PCICTRL-H.
	 * The same is true for both PCI channels.
	 */
	temp = ddb_in32(DDB_PCICTL0_H);
	temp |= 0x80000000;
	ddb_out32(DDB_PCICTL0_H, temp);
	temp &= ~0xc0000000;
	ddb_out32(DDB_PCICTL0_H, temp);

	temp = ddb_in32(DDB_PCICTL1_H);
	temp |= 0x80000000;
	ddb_out32(DDB_PCICTL1_H, temp);
	temp &= ~0xc0000000;
	ddb_out32(DDB_PCICTL1_H, temp);
}
