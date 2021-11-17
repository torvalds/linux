// SPDX-License-Identifier: GPL-2.0
/*
 * PCI operations for the Sega Dreamcast
 *
 * Copyright (C) 2001, 2002  M. R. Brown
 * Copyright (C) 2002, 2003  Paul Mundt
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <mach/pci.h>

/*
 * The !gapspci_config_access case really shouldn't happen, ever, unless
 * someone implicitly messes around with the last devfn value.. otherwise we
 * only support a single device anyways, and if we didn't have a BBA, we
 * wouldn't make it terribly far through the PCI setup anyways.
 *
 * Also, we could very easily support both Type 0 and Type 1 configurations
 * here, but since it doesn't seem that there is any such implementation in
 * existence, we don't bother.
 *
 * I suppose if someone actually gets around to ripping the chip out of
 * the BBA and hanging some more devices off of it, then this might be
 * something to take into consideration. However, due to the cost of the BBA,
 * and the general lack of activity by DC hardware hackers, this doesn't seem
 * likely to happen anytime soon.
 */
static int gapspci_config_access(unsigned char bus, unsigned int devfn)
{
	return (bus == 0) && (devfn == 0);
}

/*
 * We can also actually read and write in b/w/l sizes! Thankfully this part
 * was at least done right, and we don't have to do the stupid masking and
 * shifting that we do on the 7751! Small wonders never cease to amaze.
 */
static int gapspci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	*val = 0xffffffff;

	if (!gapspci_config_access(bus->number, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1: *val = inb(GAPSPCI_BBA_CONFIG+where); break;
	case 2: *val = inw(GAPSPCI_BBA_CONFIG+where); break;
	case 4: *val = inl(GAPSPCI_BBA_CONFIG+where); break;
	}

        return PCIBIOS_SUCCESSFUL;
}

static int gapspci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	if (!gapspci_config_access(bus->number, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1: outb(( u8)val, GAPSPCI_BBA_CONFIG+where); break;
	case 2: outw((u16)val, GAPSPCI_BBA_CONFIG+where); break;
	case 4: outl((u32)val, GAPSPCI_BBA_CONFIG+where); break;
	}

        return PCIBIOS_SUCCESSFUL;
}

struct pci_ops gapspci_pci_ops = {
	.read	= gapspci_read,
	.write	= gapspci_write,
};
