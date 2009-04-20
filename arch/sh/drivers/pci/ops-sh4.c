/*
 * Generic SH-4 / SH-4A PCIC operations (SH7751, SH7780).
 *
 * Copyright (C) 2002 - 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/pci.h>
#include <linux/io.h>
#include <asm/addrspace.h>
#include "pci-sh4.h"

/*
 * Direct access to PCI hardware...
 */
#define CONFIG_CMD(bus, devfn, where) \
	(P1SEG | (bus->number << 16) | (devfn << 8) | (where & ~3))

static DEFINE_SPINLOCK(sh4_pci_lock);

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */
static int sh4_pci_read(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 *val)
{
	struct pci_channel *chan = bus->sysdata;
	unsigned long flags;
	u32 data;

	/*
	 * PCIPDR may only be accessed as 32 bit words,
	 * so we must do byte alignment by hand
	 */
	spin_lock_irqsave(&sh4_pci_lock, flags);
	pci_write_reg(chan, CONFIG_CMD(bus, devfn, where), SH4_PCIPAR);
	data = pci_read_reg(chan, SH4_PCIPDR);
	spin_unlock_irqrestore(&sh4_pci_lock, flags);

	switch (size) {
	case 1:
		*val = (data >> ((where & 3) << 3)) & 0xff;
		break;
	case 2:
		*val = (data >> ((where & 2) << 3)) & 0xffff;
		break;
	case 4:
		*val = data;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

/*
 * Since SH4 only does 32bit access we'll have to do a read,
 * mask,write operation.
 * We'll allow an odd byte offset, though it should be illegal.
 */
static int sh4_pci_write(struct pci_bus *bus, unsigned int devfn,
			 int where, int size, u32 val)
{
	struct pci_channel *chan = bus->sysdata;
	unsigned long flags;
	int shift;
	u32 data;

	spin_lock_irqsave(&sh4_pci_lock, flags);
	pci_write_reg(chan, CONFIG_CMD(bus, devfn, where), SH4_PCIPAR);
	data = pci_read_reg(chan, SH4_PCIPDR);
	spin_unlock_irqrestore(&sh4_pci_lock, flags);

	switch (size) {
	case 1:
		shift = (where & 3) << 3;
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
		break;
	case 2:
		shift = (where & 2) << 3;
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
		break;
	case 4:
		data = val;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	pci_write_reg(chan, data, SH4_PCIPDR);

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops sh4_pci_ops = {
	.read		= sh4_pci_read,
	.write		= sh4_pci_write,
};

/*
 * Not really related to pci_ops, but it's common and not worth shoving
 * somewhere else for now..
 */
int __init sh4_pci_check_direct(struct pci_channel *chan)
{
	/*
	 * Check if configuration works.
	 */
	unsigned int tmp = pci_read_reg(chan, SH4_PCIPAR);

	pci_write_reg(chan, P1SEG, SH4_PCIPAR);

	if (pci_read_reg(chan, SH4_PCIPAR) == P1SEG) {
		pci_write_reg(chan, tmp, SH4_PCIPAR);
		printk(KERN_INFO "PCI: Using configuration type 1\n");
		request_region(chan->reg_base + SH4_PCIPAR, 8,
			       "PCI conf1");
		return 0;
	}

	pci_write_reg(chan, tmp, SH4_PCIPAR);

	printk(KERN_ERR "PCI: %s failed\n", __func__);

	return -EINVAL;
}

int __attribute__((weak)) pci_fixup_pcic(struct pci_channel *chan)
{
	/* Nothing to do. */
	return 0;
}
