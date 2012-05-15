/*
 * arch/arm/plat-iop/pci.c
 *
 * PCI support for the Intel IOP32X and IOP33X processors
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/signal.h>
#include <asm/system.h>
#include <mach/hardware.h>
#include <asm/mach/pci.h>
#include <asm/hardware/iop3xx.h>

// #define DEBUG

#ifdef DEBUG
#define  DBG(x...) printk(x)
#else
#define  DBG(x...) do { } while (0)
#endif

/*
 * This routine builds either a type0 or type1 configuration command.  If the
 * bus is on the 803xx then a type0 made, else a type1 is created.
 */
static u32 iop3xx_cfg_address(struct pci_bus *bus, int devfn, int where)
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
 * errors occur during a config cycle where there is no device, like during
 * the discovery stage.
 */
static int iop3xx_pci_status(void)
{
	unsigned int status;
	int ret = 0;

	/*
	 * Check the status registers.
	 */
	status = *IOP3XX_ATUSR;
	if (status & 0xf900) {
		DBG("\t\t\tPCI: P0 - status = 0x%08x\n", status);
		*IOP3XX_ATUSR = status & 0xf900;
		ret = 1;
	}

	status = *IOP3XX_ATUISR;
	if (status & 0x679f) {
		DBG("\t\t\tPCI: P1 - status = 0x%08x\n", status);
		*IOP3XX_ATUISR = status & 0x679f;
		ret = 1;
	}

	return ret;
}

/*
 * Simply write the address register and read the configuration
 * data.  Note that the 4 nops ensure that we are able to handle
 * a delayed abort (in theory.)
 */
static u32 iop3xx_read(unsigned long addr)
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
		: "r" (addr), "r" (IOP3XX_OCCAR), "r" (IOP3XX_OCCDR));

	return val;
}

/*
 * The read routines must check the error status of the last configuration
 * cycle.  If there was an error, the routine returns all hex f's.
 */
static int
iop3xx_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 *value)
{
	unsigned long addr = iop3xx_cfg_address(bus, devfn, where);
	u32 val = iop3xx_read(addr) >> ((where & 3) * 8);

	if (iop3xx_pci_status())
		val = 0xffffffff;

	*value = val;

	return PCIBIOS_SUCCESSFUL;
}

static int
iop3xx_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 value)
{
	unsigned long addr = iop3xx_cfg_address(bus, devfn, where);
	u32 val;

	if (size != 4) {
		val = iop3xx_read(addr);
		if (iop3xx_pci_status())
			return PCIBIOS_SUCCESSFUL;

		where = (where & 3) * 8;

		if (size == 1)
			val &= ~(0xff << where);
		else
			val &= ~(0xffff << where);

		*IOP3XX_OCCDR = val | value << where;
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
			  "r" (IOP3XX_OCCAR), "r" (IOP3XX_OCCDR));
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops iop3xx_ops = {
	.read	= iop3xx_read_config,
	.write	= iop3xx_write_config,
};

/*
 * When a PCI device does not exist during config cycles, the 80200 gets a
 * bus error instead of returning 0xffffffff. This handler simply returns.
 */
static int
iop3xx_pci_abort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
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

int iop3xx_pci_setup(int nr, struct pci_sys_data *sys)
{
	struct resource *res;

	if (nr != 0)
		return 0;

	res = kzalloc(2 * sizeof(struct resource), GFP_KERNEL);
	if (!res)
		panic("PCI: unable to alloc resources");

	res[0].start = IOP3XX_PCI_LOWER_IO_PA;
	res[0].end   = IOP3XX_PCI_LOWER_IO_PA + IOP3XX_PCI_IO_WINDOW_SIZE - 1;
	res[0].name  = "IOP3XX PCI I/O Space";
	res[0].flags = IORESOURCE_IO;
	request_resource(&ioport_resource, &res[0]);

	res[1].start = IOP3XX_PCI_LOWER_MEM_PA;
	res[1].end   = IOP3XX_PCI_LOWER_MEM_PA + IOP3XX_PCI_MEM_WINDOW_SIZE - 1;
	res[1].name  = "IOP3XX PCI Memory Space";
	res[1].flags = IORESOURCE_MEM;
	request_resource(&iomem_resource, &res[1]);

	/*
	 * Use whatever translation is already setup.
	 */
	sys->mem_offset = IOP3XX_PCI_LOWER_MEM_PA - *IOP3XX_OMWTVR0;
	sys->io_offset  = IOP3XX_PCI_LOWER_IO_PA - *IOP3XX_OIOWTVR;

	pci_add_resource_offset(&sys->resources, &res[0], sys->io_offset);
	pci_add_resource_offset(&sys->resources, &res[1], sys->mem_offset);

	return 1;
}

struct pci_bus *iop3xx_pci_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_root_bus(NULL, sys->busnr, &iop3xx_ops, sys,
				 &sys->resources);
}

void __init iop3xx_atu_setup(void)
{
	/* BAR 0 ( Disabled ) */
	*IOP3XX_IAUBAR0 = 0x0;
	*IOP3XX_IABAR0  = 0x0;
	*IOP3XX_IATVR0  = 0x0;
	*IOP3XX_IALR0   = 0x0;

	/* BAR 1 ( Disabled ) */
	*IOP3XX_IAUBAR1 = 0x0;
	*IOP3XX_IABAR1  = 0x0;
	*IOP3XX_IALR1   = 0x0;

	/* BAR 2 (1:1 mapping with Physical RAM) */
	/* Set limit and enable */
	*IOP3XX_IALR2 = ~((u32)IOP3XX_MAX_RAM_SIZE - 1) & ~0x1;
	*IOP3XX_IAUBAR2 = 0x0;

	/* Align the inbound bar with the base of memory */
	*IOP3XX_IABAR2 = PHYS_OFFSET |
			       PCI_BASE_ADDRESS_MEM_TYPE_64 |
			       PCI_BASE_ADDRESS_MEM_PREFETCH;

	*IOP3XX_IATVR2 = PHYS_OFFSET;

	/* Outbound window 0 */
	*IOP3XX_OMWTVR0 = IOP3XX_PCI_LOWER_MEM_BA;
	*IOP3XX_OUMWTVR0 = 0;

	/* Outbound window 1 */
	*IOP3XX_OMWTVR1 = IOP3XX_PCI_LOWER_MEM_BA +
			  IOP3XX_PCI_MEM_WINDOW_SIZE / 2;
	*IOP3XX_OUMWTVR1 = 0;

	/* BAR 3 ( Disabled ) */
	*IOP3XX_IAUBAR3 = 0x0;
	*IOP3XX_IABAR3  = 0x0;
	*IOP3XX_IATVR3  = 0x0;
	*IOP3XX_IALR3   = 0x0;

	/* Setup the I/O Bar
	 */
	*IOP3XX_OIOWTVR = IOP3XX_PCI_LOWER_IO_BA;

	/* Enable inbound and outbound cycles
	 */
	*IOP3XX_ATUCMD |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
			       PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	*IOP3XX_ATUCR |= IOP3XX_ATUCR_OUT_EN;
}

void __init iop3xx_atu_disable(void)
{
	*IOP3XX_ATUCMD = 0;
	*IOP3XX_ATUCR = 0;

	/* wait for cycles to quiesce */
	while (*IOP3XX_PCSR & (IOP3XX_PCSR_OUT_Q_BUSY |
				     IOP3XX_PCSR_IN_Q_BUSY))
		cpu_relax();

	/* BAR 0 ( Disabled ) */
	*IOP3XX_IAUBAR0 = 0x0;
	*IOP3XX_IABAR0  = 0x0;
	*IOP3XX_IATVR0  = 0x0;
	*IOP3XX_IALR0   = 0x0;

	/* BAR 1 ( Disabled ) */
	*IOP3XX_IAUBAR1 = 0x0;
	*IOP3XX_IABAR1  = 0x0;
	*IOP3XX_IALR1   = 0x0;

	/* BAR 2 ( Disabled ) */
	*IOP3XX_IAUBAR2 = 0x0;
	*IOP3XX_IABAR2  = 0x0;
	*IOP3XX_IATVR2  = 0x0;
	*IOP3XX_IALR2   = 0x0;

	/* BAR 3 ( Disabled ) */
	*IOP3XX_IAUBAR3 = 0x0;
	*IOP3XX_IABAR3  = 0x0;
	*IOP3XX_IATVR3  = 0x0;
	*IOP3XX_IALR3   = 0x0;

	/* Clear the outbound windows */
	*IOP3XX_OIOWTVR  = 0;

	/* Outbound window 0 */
	*IOP3XX_OMWTVR0 = 0;
	*IOP3XX_OUMWTVR0 = 0;

	/* Outbound window 1 */
	*IOP3XX_OMWTVR1 = 0;
	*IOP3XX_OUMWTVR1 = 0;
}

/* Flag to determine whether the ATU is initialized and the PCI bus scanned */
int init_atu;

int iop3xx_get_init_atu(void) {
	/* check if default has been overridden */
	if (init_atu != IOP3XX_INIT_ATU_DEFAULT)
		return init_atu;
	else
		return IOP3XX_INIT_ATU_DISABLE;
}

static void __init iop3xx_atu_debug(void)
{
	DBG("PCI: Intel IOP3xx PCI init.\n");
	DBG("PCI: Outbound memory window 0: PCI 0x%08x%08x\n",
		*IOP3XX_OUMWTVR0, *IOP3XX_OMWTVR0);
	DBG("PCI: Outbound memory window 1: PCI 0x%08x%08x\n",
		*IOP3XX_OUMWTVR1, *IOP3XX_OMWTVR1);
	DBG("PCI: Outbound IO window: PCI 0x%08x\n",
		*IOP3XX_OIOWTVR);

	DBG("PCI: Inbound memory window 0: PCI 0x%08x%08x 0x%08x -> 0x%08x\n",
		*IOP3XX_IAUBAR0, *IOP3XX_IABAR0, *IOP3XX_IALR0, *IOP3XX_IATVR0);
	DBG("PCI: Inbound memory window 1: PCI 0x%08x%08x 0x%08x\n",
		*IOP3XX_IAUBAR1, *IOP3XX_IABAR1, *IOP3XX_IALR1);
	DBG("PCI: Inbound memory window 2: PCI 0x%08x%08x 0x%08x -> 0x%08x\n",
		*IOP3XX_IAUBAR2, *IOP3XX_IABAR2, *IOP3XX_IALR2, *IOP3XX_IATVR2);
	DBG("PCI: Inbound memory window 3: PCI 0x%08x%08x 0x%08x -> 0x%08x\n",
		*IOP3XX_IAUBAR3, *IOP3XX_IABAR3, *IOP3XX_IALR3, *IOP3XX_IATVR3);

	DBG("PCI: Expansion ROM window: PCI 0x%08x%08x 0x%08x -> 0x%08x\n",
		0, *IOP3XX_ERBAR, *IOP3XX_ERLR, *IOP3XX_ERTVR);

	DBG("ATU: IOP3XX_ATUCMD=0x%04x\n", *IOP3XX_ATUCMD);
	DBG("ATU: IOP3XX_ATUCR=0x%08x\n", *IOP3XX_ATUCR);

	hook_fault_code(16+6, iop3xx_pci_abort, SIGBUS, 0, "imprecise external abort");
}

/* for platforms that might be host-bus-adapters */
void __init iop3xx_pci_preinit_cond(void)
{
	if (iop3xx_get_init_atu() == IOP3XX_INIT_ATU_ENABLE) {
		iop3xx_atu_disable();
		iop3xx_atu_setup();
		iop3xx_atu_debug();
	}
}

void __init iop3xx_pci_preinit(void)
{
	pcibios_min_io = 0;
	pcibios_min_mem = 0;

	iop3xx_atu_disable();
	iop3xx_atu_setup();
	iop3xx_atu_debug();
}

/* allow init_atu to be user overridden */
static int __init iop3xx_init_atu_setup(char *str)
{
	init_atu = IOP3XX_INIT_ATU_DEFAULT;
	if (str) {
		while (*str != '\0') {
			switch (*str) {
			case 'y':
			case 'Y':
				init_atu = IOP3XX_INIT_ATU_ENABLE;
				break;
			case 'n':
			case 'N':
				init_atu = IOP3XX_INIT_ATU_DISABLE;
				break;
			case ',':
			case '=':
				break;
			default:
				printk(KERN_DEBUG "\"%s\" malformed at "
					    "character: \'%c\'",
					    __func__,
					    *str);
				*(str + 1) = '\0';
			}
			str++;
		}
	}

	return 1;
}

__setup("iop3xx_init_atu", iop3xx_init_atu_setup);

