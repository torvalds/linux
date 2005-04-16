/*
 * arch/arm/mach-iop3xx/iop331-pci.c
 *
 * PCI support for the Intel IOP331 chipset
 *
 * Author: Dave Jiang (dave.jiang@intel.com)
 * Copyright (C) 2003, 2004 Intel Corp.
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

#include <asm/arch/iop331.h>

#undef DEBUG
#undef DEBUG1

#ifdef DEBUG
#define  DBG(x...) printk(x)
#else
#define  DBG(x...) do { } while (0)
#endif

#ifdef DEBUG1
#define  DBG1(x...) printk(x)
#else
#define  DBG1(x...) do { } while (0)
#endif

/*
 * This routine builds either a type0 or type1 configuration command.  If the
 * bus is on the 80331 then a type0 made, else a type1 is created.
 */
static u32 iop331_cfg_address(struct pci_bus *bus, int devfn, int where)
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
static int iop331_pci_status(void)
{
	unsigned int status;
	int ret = 0;

	/*
	 * Check the status registers.
	 */
	status = *IOP331_ATUSR;
	if (status & 0xf900)
	{
		DBG("\t\t\tPCI: P0 - status = 0x%08x\n", status);
		*IOP331_ATUSR = status & 0xf900;
		ret = 1;
	}
	status = *IOP331_ATUISR;
	if (status & 0x679f)
	{
		DBG("\t\t\tPCI: P1 - status = 0x%08x\n", status);
		*IOP331_ATUISR = status & 0x679f;
		ret = 1;
	}
	return ret;
}

/*
 * Simply write the address register and read the configuration
 * data.  Note that the 4 nop's ensure that we are able to handle
 * a delayed abort (in theory.)
 */
static inline u32 iop331_read(unsigned long addr)
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
		: "r" (addr), "r" (IOP331_OCCAR), "r" (IOP331_OCCDR));

	return val;
}

/*
 * The read routines must check the error status of the last configuration
 * cycle.  If there was an error, the routine returns all hex f's.
 */
static int
iop331_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 *value)
{
	unsigned long addr = iop331_cfg_address(bus, devfn, where);
	u32 val = iop331_read(addr) >> ((where & 3) * 8);

	if( iop331_pci_status() )
		val = 0xffffffff;

	*value = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop331_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 value)
{
	unsigned long addr = iop331_cfg_address(bus, devfn, where);
	u32 val;

	if (size != 4) {
		val = iop331_read(addr);
		if (!iop331_pci_status() == 0)
			return PCIBIOS_SUCCESSFUL;

		where = (where & 3) * 8;

		if (size == 1)
			val &= ~(0xff << where);
		else
			val &= ~(0xffff << where);

		*IOP331_OCCDR = val | value << where;
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
			  "r" (IOP331_OCCAR), "r" (IOP331_OCCDR));
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop331_ops = {
	.read	= iop331_read_config,
	.write	= iop331_write_config,
};

/*
 * When a PCI device does not exist during config cycles, the XScale gets a
 * bus error instead of returning 0xffffffff. This handler simply returns.
 */
int
iop331_pci_abort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
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
 * Scan an IOP331 PCI bus.  sys->bus defines which bus we scan.
 */
struct pci_bus *iop331_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_bus(sys->busnr, &iop331_ops, sys);
}

void iop331_init(void)
{
	DBG1("PCI:  Intel 80331 PCI init code.\n");
	DBG1("\tATU: IOP331_ATUCMD=0x%04x\n", *IOP331_ATUCMD);
	DBG1("\tATU: IOP331_OMWTVR0=0x%04x, IOP331_OIOWTVR=0x%04x\n",
			*IOP331_OMWTVR0,
			*IOP331_OIOWTVR);
	DBG1("\tATU: IOP331_OMWTVR1=0x%04x\n", *IOP331_OMWTVR1);
	DBG1("\tATU: IOP331_ATUCR=0x%08x\n", *IOP331_ATUCR);
	DBG1("\tATU: IOP331_IABAR0=0x%08x IOP331_IALR0=0x%08x IOP331_IATVR0=%08x\n", *IOP331_IABAR0, *IOP331_IALR0, *IOP331_IATVR0);
	DBG1("\tATU: IOP31_IABAR1=0x%08x IOP331_IALR1=0x%08x\n", *IOP331_IABAR1, *IOP331_IALR1);
	DBG1("\tATU: IOP331_ERBAR=0x%08x IOP331_ERLR=0x%08x IOP331_ERTVR=%08x\n", *IOP331_ERBAR, *IOP331_ERLR, *IOP331_ERTVR);
	DBG1("\tATU: IOP331_IABAR2=0x%08x IOP331_IALR2=0x%08x IOP331_IATVR2=%08x\n", *IOP331_IABAR2, *IOP331_IALR2, *IOP331_IATVR2);
	DBG1("\tATU: IOP331_IABAR3=0x%08x IOP331_IALR3=0x%08x IOP331_IATVR3=%08x\n", *IOP331_IABAR3, *IOP331_IALR3, *IOP331_IATVR3);

	hook_fault_code(16+6, iop331_pci_abort, SIGBUS, "imprecise external abort");
}

