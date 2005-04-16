/*
 * arch/arm/mach-iop3xx/iop321-pci.c
 *
 * PCI support for the Intel IOP321 chipset
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/mach/pci.h>

#include <asm/arch/iop321.h>

// #define DEBUG

#ifdef DEBUG
#define  DBG(x...) printk(x)
#else
#define  DBG(x...) do { } while (0)
#endif

/*
 * This routine builds either a type0 or type1 configuration command.  If the
 * bus is on the 80321 then a type0 made, else a type1 is created.
 */
static u32 iop321_cfg_address(struct pci_bus *bus, int devfn, int where)
{
	struct pci_sys_data *sys = bus->sysdata;
	u32 addr;

	if (sys->busnr == bus->number)
		addr = 1 << (PCI_SLOT(devfn) + 16) | (PCI_SLOT(devfn) << 11);
	else
		addr = bus->number << 16 | PCI_SLOT(devfn) << 11 | 1;

	addr |=	PCI_FUNC(devfn) << 8 | (where & ~3);

	return addr;
}

/*
 * This routine checks the status of the last configuration cycle.  If an error
 * was detected it returns a 1, else it returns a 0.  The errors being checked
 * are parity, master abort, target abort (master and target).  These types of
 * errors occure during a config cycle where there is no device, like during
 * the discovery stage.
 */
static int iop321_pci_status(void)
{
	unsigned int status;
	int ret = 0;

	/*
	 * Check the status registers.
	 */
	status = *IOP321_ATUSR;
	if (status & 0xf900)
	{
		DBG("\t\t\tPCI: P0 - status = 0x%08x\n", status);
		*IOP321_ATUSR = status & 0xf900;
		ret = 1;
	}
	status = *IOP321_ATUISR;
	if (status & 0x679f)
	{
		DBG("\t\t\tPCI: P1 - status = 0x%08x\n", status);
		*IOP321_ATUISR = status & 0x679f;
		ret = 1;
	}
	return ret;
}

/*
 * Simply write the address register and read the configuration
 * data.  Note that the 4 nop's ensure that we are able to handle
 * a delayed abort (in theory.)
 */
static inline u32 iop321_read(unsigned long addr)
{
	u32 val;

	__asm__ __volatile__(
		"str	%1, [%2]\n\t"
		"ldr	%0, [%3]\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		: "=r" (val)
		: "r" (addr), "r" (IOP321_OCCAR), "r" (IOP321_OCCDR));

	return val;
}

/*
 * The read routines must check the error status of the last configuration
 * cycle.  If there was an error, the routine returns all hex f's.
 */
static int
iop321_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 *value)
{
	unsigned long addr = iop321_cfg_address(bus, devfn, where);
	u32 val = iop321_read(addr) >> ((where & 3) * 8);

	if( iop321_pci_status() )
		val = 0xffffffff;

	*value = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop321_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 value)
{
	unsigned long addr = iop321_cfg_address(bus, devfn, where);
	u32 val;

	if (size != 4) {
		val = iop321_read(addr);
		if (!iop321_pci_status() == 0)
			return PCIBIOS_SUCCESSFUL;

		where = (where & 3) * 8;

		if (size == 1)
			val &= ~(0xff << where);
		else
			val &= ~(0xffff << where);

		*IOP321_OCCDR = val | value << where;
	} else {
		asm volatile(
			"str	%1, [%2]\n\t"
			"str	%0, [%3]\n\t"
			"nop\n\t"
			"nop\n\t"
			"nop\n\t"
			"nop\n\t"
			:
			: "r" (value), "r" (addr),
			  "r" (IOP321_OCCAR), "r" (IOP321_OCCDR));
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop321_ops = {
	.read	= iop321_read_config,
	.write	= iop321_write_config,
};

/*
 * When a PCI device does not exist during config cycles, the 80200 gets a
 * bus error instead of returning 0xffffffff. This handler simply returns.
 */
int
iop321_pci_abort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	DBG("PCI abort: address = 0x%08lx fsr = 0x%03x PC = 0x%08lx LR = 0x%08lx\n",
		addr, fsr, regs->ARM_pc, regs->ARM_lr);

	/*
	 * If it was an imprecise abort, then we need to correct the
	 * return address to be _after_ the instruction.
	 */
	if (fsr & (1 << 10))
		regs->ARM_pc += 4;

	return 0;
}

/*
 * Scan an IOP321 PCI bus.  sys->bus defines which bus we scan.
 */
struct pci_bus *iop321_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_bus(sys->busnr, &iop321_ops, sys);
}

void iop321_init(void)
{
	DBG("PCI:  Intel 80321 PCI init code.\n");
	DBG("ATU: IOP321_ATUCMD=0x%04x\n", *IOP321_ATUCMD);
	DBG("ATU: IOP321_OMWTVR0=0x%04x, IOP321_OIOWTVR=0x%04x\n",
			*IOP321_OMWTVR0,
			*IOP321_OIOWTVR);
	DBG("ATU: IOP321_ATUCR=0x%08x\n", *IOP321_ATUCR);
	DBG("ATU: IOP321_IABAR0=0x%08x IOP321_IALR0=0x%08x IOP321_IATVR0=%08x\n",
			*IOP321_IABAR0, *IOP321_IALR0, *IOP321_IATVR0);
	DBG("ATU: IOP321_OMWTVR0=0x%08x\n", *IOP321_OMWTVR0);
	DBG("ATU: IOP321_IABAR1=0x%08x IOP321_IALR1=0x%08x\n",
			*IOP321_IABAR1, *IOP321_IALR1);
	DBG("ATU: IOP321_ERBAR=0x%08x IOP321_ERLR=0x%08x IOP321_ERTVR=%08x\n",
			*IOP321_ERBAR, *IOP321_ERLR, *IOP321_ERTVR);
	DBG("ATU: IOP321_IABAR2=0x%08x IOP321_IALR2=0x%08x IOP321_IATVR2=%08x\n",
			*IOP321_IABAR2, *IOP321_IALR2, *IOP321_IATVR2);
	DBG("ATU: IOP321_IABAR3=0x%08x IOP321_IALR3=0x%08x IOP321_IATVR3=%08x\n",
			*IOP321_IABAR3, *IOP321_IALR3, *IOP321_IATVR3);

	hook_fault_code(16+6, iop321_pci_abort, SIGBUS, "imprecise external abort");
}

