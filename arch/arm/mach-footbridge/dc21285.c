/*
 *  linux/arch/arm/kernel/dec21285.c: PCI functions for DC21285
 *
 *  Copyright (C) 1998-2001 Russell King
 *  Copyright (C) 1998-2000 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <video/vga.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <asm/mach/pci.h>
#include <asm/hardware/dec21285.h>

#define MAX_SLOTS		21

#define PCICMD_ABORT		((PCI_STATUS_REC_MASTER_ABORT| \
				  PCI_STATUS_REC_TARGET_ABORT)<<16)

#define PCICMD_ERROR_BITS	((PCI_STATUS_DETECTED_PARITY | \
				  PCI_STATUS_REC_MASTER_ABORT | \
				  PCI_STATUS_REC_TARGET_ABORT | \
				  PCI_STATUS_PARITY) << 16)

extern int setup_arm_irq(int, struct irqaction *);
extern void pcibios_report_status(u_int status_mask, int warn);

static unsigned long
dc21285_base_address(struct pci_bus *bus, unsigned int devfn)
{
	unsigned long addr = 0;

	if (bus->number == 0) {
		if (PCI_SLOT(devfn) == 0)
			/*
			 * For devfn 0, point at the 21285
			 */
			addr = ARMCSR_BASE;
		else {
			devfn -= 1 << 3;

			if (devfn < PCI_DEVFN(MAX_SLOTS, 0))
				addr = PCICFG0_BASE | 0xc00000 | (devfn << 8);
		}
	} else
		addr = PCICFG1_BASE | (bus->number << 16) | (devfn << 8);

	return addr;
}

static int
dc21285_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 *value)
{
	unsigned long addr = dc21285_base_address(bus, devfn);
	u32 v = 0xffffffff;

	if (addr)
		switch (size) {
		case 1:
			asm("ldrb	%0, [%1, %2]"
				: "=r" (v) : "r" (addr), "r" (where) : "cc");
			break;
		case 2:
			asm("ldrh	%0, [%1, %2]"
				: "=r" (v) : "r" (addr), "r" (where) : "cc");
			break;
		case 4:
			asm("ldr	%0, [%1, %2]"
				: "=r" (v) : "r" (addr), "r" (where) : "cc");
			break;
		}

	*value = v;

	v = *CSR_PCICMD;
	if (v & PCICMD_ABORT) {
		*CSR_PCICMD = v & (0xffff|PCICMD_ABORT);
		return -1;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int
dc21285_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 value)
{
	unsigned long addr = dc21285_base_address(bus, devfn);
	u32 v;

	if (addr)
		switch (size) {
		case 1:
			asm("strb	%0, [%1, %2]"
				: : "r" (value), "r" (addr), "r" (where)
				: "cc");
			break;
		case 2:
			asm("strh	%0, [%1, %2]"
				: : "r" (value), "r" (addr), "r" (where)
				: "cc");
			break;
		case 4:
			asm("str	%0, [%1, %2]"
				: : "r" (value), "r" (addr), "r" (where)
				: "cc");
			break;
		}

	v = *CSR_PCICMD;
	if (v & PCICMD_ABORT) {
		*CSR_PCICMD = v & (0xffff|PCICMD_ABORT);
		return -1;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops dc21285_ops = {
	.read	= dc21285_read_config,
	.write	= dc21285_write_config,
};

static struct timer_list serr_timer;
static struct timer_list perr_timer;

static void dc21285_enable_error(unsigned long __data)
{
	switch (__data) {
	case IRQ_PCI_SERR:
		del_timer(&serr_timer);
		break;

	case IRQ_PCI_PERR:
		del_timer(&perr_timer);
		break;
	}

	enable_irq(__data);
}

/*
 * Warn on PCI errors.
 */
static irqreturn_t dc21285_abort_irq(int irq, void *dev_id)
{
	unsigned int cmd;
	unsigned int status;

	cmd = *CSR_PCICMD;
	status = cmd >> 16;
	cmd = cmd & 0xffff;

	if (status & PCI_STATUS_REC_MASTER_ABORT) {
		printk(KERN_DEBUG "PCI: master abort, pc=0x%08lx\n",
			instruction_pointer(get_irq_regs()));
		cmd |= PCI_STATUS_REC_MASTER_ABORT << 16;
	}

	if (status & PCI_STATUS_REC_TARGET_ABORT) {
		printk(KERN_DEBUG "PCI: target abort: ");
		pcibios_report_status(PCI_STATUS_REC_MASTER_ABORT |
				      PCI_STATUS_SIG_TARGET_ABORT |
				      PCI_STATUS_REC_TARGET_ABORT, 1);
		printk("\n");

		cmd |= PCI_STATUS_REC_TARGET_ABORT << 16;
	}

	*CSR_PCICMD = cmd;

	return IRQ_HANDLED;
}

static irqreturn_t dc21285_serr_irq(int irq, void *dev_id)
{
	struct timer_list *timer = dev_id;
	unsigned int cntl;

	printk(KERN_DEBUG "PCI: system error received: ");
	pcibios_report_status(PCI_STATUS_SIG_SYSTEM_ERROR, 1);
	printk("\n");

	cntl = *CSR_SA110_CNTL & 0xffffdf07;
	*CSR_SA110_CNTL = cntl | SA110_CNTL_RXSERR;

	/*
	 * back off this interrupt
	 */
	disable_irq(irq);
	timer->expires = jiffies + HZ;
	add_timer(timer);

	return IRQ_HANDLED;
}

static irqreturn_t dc21285_discard_irq(int irq, void *dev_id)
{
	printk(KERN_DEBUG "PCI: discard timer expired\n");
	*CSR_SA110_CNTL &= 0xffffde07;

	return IRQ_HANDLED;
}

static irqreturn_t dc21285_dparity_irq(int irq, void *dev_id)
{
	unsigned int cmd;

	printk(KERN_DEBUG "PCI: data parity error detected: ");
	pcibios_report_status(PCI_STATUS_PARITY | PCI_STATUS_DETECTED_PARITY, 1);
	printk("\n");

	cmd = *CSR_PCICMD & 0xffff;
	*CSR_PCICMD = cmd | 1 << 24;

	return IRQ_HANDLED;
}

static irqreturn_t dc21285_parity_irq(int irq, void *dev_id)
{
	struct timer_list *timer = dev_id;
	unsigned int cmd;

	printk(KERN_DEBUG "PCI: parity error detected: ");
	pcibios_report_status(PCI_STATUS_PARITY | PCI_STATUS_DETECTED_PARITY, 1);
	printk("\n");

	cmd = *CSR_PCICMD & 0xffff;
	*CSR_PCICMD = cmd | 1 << 31;

	/*
	 * back off this interrupt
	 */
	disable_irq(irq);
	timer->expires = jiffies + HZ;
	add_timer(timer);

	return IRQ_HANDLED;
}

int __init dc21285_setup(int nr, struct pci_sys_data *sys)
{
	struct resource *res;

	if (nr || !footbridge_cfn_mode())
		return 0;

	res = kzalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res) {
		printk("out of memory for root bus resources");
		return 0;
	}

	res[0].flags = IORESOURCE_MEM;
	res[0].name  = "Footbridge non-prefetch";
	res[1].flags = IORESOURCE_MEM | IORESOURCE_PREFETCH;
	res[1].name  = "Footbridge prefetch";

	allocate_resource(&iomem_resource, &res[1], 0x20000000,
			  0xa0000000, 0xffffffff, 0x20000000, NULL, NULL);
	allocate_resource(&iomem_resource, &res[0], 0x40000000,
			  0x80000000, 0xffffffff, 0x40000000, NULL, NULL);

	sys->mem_offset  = DC21285_PCI_MEM;

	pci_add_resource_offset(&sys->resources,
				&ioport_resource, sys->io_offset);
	pci_add_resource_offset(&sys->resources, &res[0], sys->mem_offset);
	pci_add_resource_offset(&sys->resources, &res[1], sys->mem_offset);

	return 1;
}

struct pci_bus * __init dc21285_scan_bus(int nr, struct pci_sys_data *sys)
{
	return pci_scan_root_bus(NULL, 0, &dc21285_ops, sys, &sys->resources);
}

#define dc21285_request_irq(_a, _b, _c, _d, _e) \
	WARN_ON(request_irq(_a, _b, _c, _d, _e) < 0)

void __init dc21285_preinit(void)
{
	unsigned int mem_size, mem_mask;
	int cfn_mode;

	pcibios_min_mem = 0x81000000;
	vga_base = PCIMEM_BASE;

	mem_size = (unsigned int)high_memory - PAGE_OFFSET;
	for (mem_mask = 0x00100000; mem_mask < 0x10000000; mem_mask <<= 1)
		if (mem_mask >= mem_size)
			break;		

	/*
	 * These registers need to be set up whether we're the
	 * central function or not.
	 */
	*CSR_SDRAMBASEMASK    = (mem_mask - 1) & 0x0ffc0000;
	*CSR_SDRAMBASEOFFSET  = 0;
	*CSR_ROMBASEMASK      = 0x80000000;
	*CSR_CSRBASEMASK      = 0;
	*CSR_CSRBASEOFFSET    = 0;
	*CSR_PCIADDR_EXTN     = 0;

	cfn_mode = __footbridge_cfn_mode();

	printk(KERN_INFO "PCI: DC21285 footbridge, revision %02lX, in "
		"%s mode\n", *CSR_CLASSREV & 0xff, cfn_mode ?
		"central function" : "addin");

	if (footbridge_cfn_mode()) {
		/*
		 * Clear any existing errors - we aren't
		 * interested in historical data...
		 */
		*CSR_SA110_CNTL	= (*CSR_SA110_CNTL & 0xffffde07) |
				  SA110_CNTL_RXSERR;
		*CSR_PCICMD = (*CSR_PCICMD & 0xffff) | PCICMD_ERROR_BITS;
	}

	init_timer(&serr_timer);
	init_timer(&perr_timer);

	serr_timer.data = IRQ_PCI_SERR;
	serr_timer.function = dc21285_enable_error;
	perr_timer.data = IRQ_PCI_PERR;
	perr_timer.function = dc21285_enable_error;

	/*
	 * We don't care if these fail.
	 */
	dc21285_request_irq(IRQ_PCI_SERR, dc21285_serr_irq, IRQF_DISABLED,
			    "PCI system error", &serr_timer);
	dc21285_request_irq(IRQ_PCI_PERR, dc21285_parity_irq, IRQF_DISABLED,
			    "PCI parity error", &perr_timer);
	dc21285_request_irq(IRQ_PCI_ABORT, dc21285_abort_irq, IRQF_DISABLED,
			    "PCI abort", NULL);
	dc21285_request_irq(IRQ_DISCARD_TIMER, dc21285_discard_irq, IRQF_DISABLED,
			    "Discard timer", NULL);
	dc21285_request_irq(IRQ_PCI_DPERR, dc21285_dparity_irq, IRQF_DISABLED,
			    "PCI data parity", NULL);

	if (cfn_mode) {
		static struct resource csrio;

		csrio.flags  = IORESOURCE_IO;
		csrio.name   = "Footbridge";

		allocate_resource(&ioport_resource, &csrio, 128,
				  0xff00, 0xffff, 128, NULL, NULL);

		/*
		 * Map our SDRAM at a known address in PCI space, just in case
		 * the firmware had other ideas.  Using a nonzero base is
		 * necessary, since some VGA cards forcefully use PCI addresses
		 * in the range 0x000a0000 to 0x000c0000. (eg, S3 cards).
		 */
		*CSR_PCICSRBASE       = 0xf4000000;
		*CSR_PCICSRIOBASE     = csrio.start;
		*CSR_PCISDRAMBASE     = __virt_to_bus(PAGE_OFFSET);
		*CSR_PCIROMBASE       = 0;
		*CSR_PCICMD = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
			      PCI_COMMAND_INVALIDATE | PCICMD_ERROR_BITS;
	} else if (footbridge_cfn_mode() != 0) {
		/*
		 * If we are not compiled to accept "add-in" mode, then
		 * we are using a constant virt_to_bus translation which
		 * can not hope to cater for the way the host BIOS  has
		 * set up the machine.
		 */
		panic("PCI: this kernel is compiled for central "
			"function mode only");
	}
}

void __init dc21285_postinit(void)
{
	register_isa_ports(DC21285_PCI_MEM, DC21285_PCI_IO, 0);
}
