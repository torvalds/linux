/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2001 Keith M Wesolowski
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

#undef DEBUG_MACE_PCI

/*
 * Handle errors from the bridge.  This includes master and target aborts,
 * various command and address errors, and the interrupt test.  This gets
 * registered on the bridge error irq.  It's conceivable that some of these
 * conditions warrant a panic.  Anybody care to say which ones?
 */
static irqreturn_t macepci_error(int irq, void *dev, struct pt_regs *regs)
{
	char s;
	unsigned int flags = mace->pci.error;
	unsigned int addr = mace->pci.error_addr;

	if (flags & MACEPCI_ERROR_MEMORY_ADDR)
		s = 'M';
	else if (flags & MACEPCI_ERROR_CONFIG_ADDR)
		s = 'C';
	else
		s = 'X';

	if (flags & MACEPCI_ERROR_MASTER_ABORT) {
		printk("MACEPCI: Master abort at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_MASTER_ABORT;
	}
	if (flags & MACEPCI_ERROR_TARGET_ABORT) {
		printk("MACEPCI: Target abort at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_TARGET_ABORT;
	}
	if (flags & MACEPCI_ERROR_DATA_PARITY_ERR) {
		printk("MACEPCI: Data parity error at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_DATA_PARITY_ERR;
	}
	if (flags & MACEPCI_ERROR_RETRY_ERR) {
		printk("MACEPCI: Retry error at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_RETRY_ERR;
	}
	if (flags & MACEPCI_ERROR_ILLEGAL_CMD) {
		printk("MACEPCI: Illegal command at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_ILLEGAL_CMD;
	}
	if (flags & MACEPCI_ERROR_SYSTEM_ERR) {
		printk("MACEPCI: System error at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_SYSTEM_ERR;
	}
	if (flags & MACEPCI_ERROR_PARITY_ERR) {
		printk("MACEPCI: Parity error at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_PARITY_ERR;
	}
	if (flags & MACEPCI_ERROR_OVERRUN) {
		printk("MACEPCI: Overrun error at 0x%08x (%c)\n", addr, s);
		flags &= ~MACEPCI_ERROR_OVERRUN;
	}
	if (flags & MACEPCI_ERROR_SIG_TABORT) {
		printk("MACEPCI: Signaled target abort (clearing)\n");
		flags &= ~MACEPCI_ERROR_SIG_TABORT;
	}
	if (flags & MACEPCI_ERROR_INTERRUPT_TEST) {
		printk("MACEPCI: Interrupt test triggered (clearing)\n");
		flags &= ~MACEPCI_ERROR_INTERRUPT_TEST;
	}

	mace->pci.error = flags;

	return IRQ_HANDLED;
}


extern struct pci_ops mace_pci_ops;
#ifdef CONFIG_64BIT
static struct resource mace_pci_mem_resource = {
	.name	= "SGI O2 PCI MEM",
	.start	= MACEPCI_HI_MEMORY,
	.end	= 0x2FFFFFFFFUL,
	.flags	= IORESOURCE_MEM,
};
static struct resource mace_pci_io_resource = {
	.name	= "SGI O2 PCI IO",
	.start	= 0x00000000UL,
	.end	= 0xffffffffUL,
	.flags	= IORESOURCE_IO,
};
#define MACE_PCI_MEM_OFFSET 0x200000000
#else
static struct resource mace_pci_mem_resource = {
	.name	= "SGI O2 PCI MEM",
	.start	= MACEPCI_LOW_MEMORY,
	.end	= MACEPCI_LOW_MEMORY + 0x2000000 - 1,
	.flags	= IORESOURCE_MEM,
};
static struct resource mace_pci_io_resource = {
	.name	= "SGI O2 PCI IO",
	.start	= 0x00000000,
	.end	= 0xFFFFFFFF,
	.flags	= IORESOURCE_IO,
};
#define MACE_PCI_MEM_OFFSET (MACEPCI_LOW_MEMORY - 0x80000000)
#endif
static struct pci_controller mace_pci_controller = {
	.pci_ops	= &mace_pci_ops,
	.mem_resource	= &mace_pci_mem_resource,
	.io_resource	= &mace_pci_io_resource,
	.iommu		= 0,
	.mem_offset	= MACE_PCI_MEM_OFFSET,
	.io_offset	= 0,
};

static int __init mace_init(void)
{
	PCIBIOS_MIN_IO = 0x1000;

	/* Clear any outstanding errors and enable interrupts */
	mace->pci.error_addr = 0;
	mace->pci.error = 0;
	mace->pci.control = 0xff008500;

	printk("MACE PCI rev %d\n", mace->pci.rev);

	BUG_ON(request_irq(MACE_PCI_BRIDGE_IRQ, macepci_error, 0,
			   "MACE PCI error", NULL));

	iomem_resource = mace_pci_mem_resource;
	ioport_resource = mace_pci_io_resource;

	register_pci_controller(&mace_pci_controller);

	return 0;
}

arch_initcall(mace_init);
