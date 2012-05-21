/*
 * arch/arm/mach-ixp23xx/pci.c
 *
 * PCI routines for IXP23XX based systems
 *
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 * based on original code:
 *
 * Author: Naeem Afzal <naeem.m.afzal@intel.com>
 * Copyright 2002-2005 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/sizes.h>
#include <asm/mach/pci.h>
#include <mach/hardware.h>

extern int (*external_fault) (unsigned long, struct pt_regs *);

static volatile int pci_master_aborts = 0;

#ifdef DEBUG
#define DBG(x...)	printk(x)
#else
#define DBG(x...)
#endif

int clear_master_aborts(void);

static u32
*ixp23xx_pci_config_addr(unsigned int bus_nr, unsigned int devfn, int where)
{
	u32 *paddress;

	/*
	 * Must be dword aligned
	 */
	where &= ~3;

	/*
	 * For top bus, generate type 0, else type 1
	 */
	if (!bus_nr) {
		if (PCI_SLOT(devfn) >= 8)
			return 0;

		paddress = (u32 *) (IXP23XX_PCI_CFG0_VIRT
				    | (1 << (PCI_SLOT(devfn) + 16))
				    | (PCI_FUNC(devfn) << 8) | where);
	} else {
		paddress = (u32 *) (IXP23XX_PCI_CFG1_VIRT
				    | (bus_nr << 16)
				    | (PCI_SLOT(devfn) << 11)
				    | (PCI_FUNC(devfn) << 8) | where);
	}

	return paddress;
}

/*
 * Mask table, bits to mask for quantity of size 1, 2 or 4 bytes.
 * 0 and 3 are not valid indexes...
 */
static u32 bytemask[] = {
	/*0*/	0,
	/*1*/	0xff,
	/*2*/	0xffff,
	/*3*/	0,
	/*4*/	0xffffffff,
};

static int ixp23xx_pci_read_config(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *value)
{
	u32 n;
	u32 *addr;

	n = where % 4;

	DBG("In config_read(%d) %d from dev %d:%d:%d\n", size, where,
		bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));

	addr = ixp23xx_pci_config_addr(bus->number, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	pci_master_aborts = 0;
	*value = (*addr >> (8*n)) & bytemask[size];
	if (pci_master_aborts) {
			pci_master_aborts = 0;
			*value = 0xffffffff;
			return PCIBIOS_DEVICE_NOT_FOUND;
		}

	return PCIBIOS_SUCCESSFUL;
}

/*
 * We don't do error checking on the address for writes.
 * It's assumed that the user checked for the device existing first
 * by doing a read first.
 */
static int ixp23xx_pci_write_config(struct pci_bus *bus, unsigned int devfn,
					int where, int size, u32 value)
{
	u32 mask;
	u32 *addr;
	u32 temp;

	mask = ~(bytemask[size] << ((where % 0x4) * 8));
	addr = ixp23xx_pci_config_addr(bus->number, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	temp = (u32) (value) << ((where % 0x4) * 8);
	*addr = (*addr & mask) | temp;

	clear_master_aborts();

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops ixp23xx_pci_ops = {
	.read	= ixp23xx_pci_read_config,
	.write	= ixp23xx_pci_write_config,
};

int ixp23xx_pci_abort_handler(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	volatile unsigned long temp;
	unsigned long flags;

	pci_master_aborts = 1;

	local_irq_save(flags);
	temp = *IXP23XX_PCI_CONTROL;

	/*
	 * master abort and cmd tgt err
	 */
	if (temp & ((1 << 8) | (1 << 5)))
		*IXP23XX_PCI_CONTROL = temp;

	temp = *IXP23XX_PCI_CMDSTAT;

	if (temp & (1 << 29))
		*IXP23XX_PCI_CMDSTAT = temp;
	local_irq_restore(flags);

	/*
	 * If it was an imprecise abort, then we need to correct the
	 * return address to be _after_ the instruction.
	 */
	if (fsr & (1 << 10))
		regs->ARM_pc += 4;

	return 0;
}

int clear_master_aborts(void)
{
	volatile u32 temp;

	temp = *IXP23XX_PCI_CONTROL;

	/*
	 * master abort and cmd tgt err
	 */
	if (temp & ((1 << 8) | (1 << 5)))
		*IXP23XX_PCI_CONTROL = temp;

	temp = *IXP23XX_PCI_CMDSTAT;

	if (temp & (1 << 29))
		*IXP23XX_PCI_CMDSTAT = temp;

	return 0;
}

static void __init ixp23xx_pci_common_init(void)
{
#ifdef __ARMEB__
	*IXP23XX_PCI_CONTROL |= 0x20000;	/* set I/O swapping */
#endif
	/*
	 * ADDR_31 needs to be clear for PCI memory access to CPP memory
	 */
	*IXP23XX_CPP2XSI_CURR_XFER_REG3 &= ~IXP23XX_CPP2XSI_ADDR_31;
	*IXP23XX_CPP2XSI_CURR_XFER_REG3 |= IXP23XX_CPP2XSI_PSH_OFF;

	/*
	 * Select correct memory for PCI inbound transactions
	 */
	if (ixp23xx_cpp_boot()) {
		*IXP23XX_PCI_CPP_ADDR_BITS &= ~(1 << 1);
	} else {
		*IXP23XX_PCI_CPP_ADDR_BITS |= (1 << 1);

		/*
		 * Enable coherency on A2 silicon.
		 */
		if (arch_is_coherent())
			*IXP23XX_CPP2XSI_CURR_XFER_REG3 &= ~IXP23XX_CPP2XSI_COH_OFF;
	}
}

void __init ixp23xx_pci_preinit(void)
{
	pcibios_min_io = 0;
	pcibios_min_mem = 0xe0000000;

	pci_set_flags(0);

	ixp23xx_pci_common_init();

	hook_fault_code(16+6, ixp23xx_pci_abort_handler, SIGBUS, 0,
			"PCI config cycle to non-existent device");

	*IXP23XX_PCI_ADDR_EXT = 0x0000e000;
}

/*
 * Prevent PCI layer from seeing the inbound host-bridge resources
 */
static void __devinit pci_fixup_ixp23xx(struct pci_dev *dev)
{
	int i;

	dev->class &= 0xff;
	dev->class |= PCI_CLASS_BRIDGE_HOST << 8;
	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		dev->resource[i].start = 0;
		dev->resource[i].end   = 0;
		dev->resource[i].flags = 0;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, 0x9002, pci_fixup_ixp23xx);

/*
 * IXP2300 systems often have large resource requirements, so we just
 * use our own resource space.
 */
static struct resource ixp23xx_pci_mem_space = {
	.start	= IXP23XX_PCI_MEM_START,
	.end	= IXP23XX_PCI_MEM_START + IXP23XX_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM,
	.name	= "PCI Mem Space"
};

static struct resource ixp23xx_pci_io_space = {
	.start	= 0x00000100,
	.end	= 0x01ffffff,
	.flags	= IORESOURCE_IO,
	.name	= "PCI I/O Space"
};

int ixp23xx_pci_setup(int nr, struct pci_sys_data *sys)
{
	if (nr >= 1)
		return 0;

	pci_add_resource_offset(&sys->resources,
				&ixp23xx_pci_io_space, sys->io_offset);
	pci_add_resource_offset(&sys->resources,
				&ixp23xx_pci_mem_space, sys->mem_offset);

	return 1;
}

void __init ixp23xx_pci_slave_init(void)
{
	ixp23xx_pci_common_init();
}
